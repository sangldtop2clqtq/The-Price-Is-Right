BEGIN;
SELECT pg_advisory_xact_lock(505202);
CREATE TABLE IF NOT EXISTS matches (
    id bigint PRIMARY KEY CHECK(id > 0),
    history_seq bigint GENERATED ALWAYS AS IDENTITY UNIQUE,
    room_code_snapshot text NOT NULL CHECK(room_code_snapshot ~ '^[A-Z0-9]{6}$'),
    status smallint NOT NULL CHECK(status IN (2,3)),
    started_at timestamptz NOT NULL,
    ended_at timestamptz NOT NULL CHECK(ended_at >= started_at),
    abort_reason smallint NOT NULL CHECK(abort_reason BETWEEN 0 AND 2),
    snapshot jsonb NOT NULL,
    CHECK ((status=2 AND abort_reason=0) OR (status=3 AND abort_reason IN (1,2)))
);
CREATE TABLE IF NOT EXISTS match_players (
    match_id bigint NOT NULL REFERENCES matches(id) ON DELETE CASCADE,
    user_id bigint NOT NULL REFERENCES users(id),
    display_name_snapshot text NOT NULL CHECK(octet_length(display_name_snapshot) BETWEEN 1 AND 96),
    total_score integer NOT NULL CHECK(total_score BETWEEN 0 AND 5000),
    rank smallint NOT NULL CHECK(rank BETWEEN 0 AND 4),
    disconnected boolean NOT NULL,
    PRIMARY KEY(match_id,user_id)
);
CREATE INDEX IF NOT EXISTS match_players_user_idx ON match_players(user_id,match_id);
CREATE TABLE IF NOT EXISTS rounds (
    id bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    match_id bigint NOT NULL REFERENCES matches(id) ON DELETE CASCADE,
    round_no smallint NOT NULL CHECK(round_no BETWEEN 1 AND 5),
    round_type smallint NOT NULL CHECK(round_type IN (0,1)),
    round_state smallint NOT NULL CHECK(round_state IN (1,2)),
    duration_ms integer NOT NULL CHECK(duration_ms BETWEEN 50 AND 60000),
    actual_total_vnd bigint NOT NULL CHECK(actual_total_vnd BETWEEN 1 AND 1000000000000),
    UNIQUE(match_id,round_no), UNIQUE(id,match_id)
);
CREATE TABLE IF NOT EXISTS round_items (
    round_id bigint NOT NULL REFERENCES rounds(id) ON DELETE CASCADE,
    item_no smallint NOT NULL CHECK(item_no BETWEEN 1 AND 3),
    product_id bigint REFERENCES products(id) ON DELETE SET NULL,
    product_name_snapshot text NOT NULL CHECK(octet_length(product_name_snapshot) BETWEEN 1 AND 96),
    image_key_snapshot text NOT NULL CHECK(octet_length(image_key_snapshot) BETWEEN 1 AND 64),
    unit_price_vnd_snapshot bigint NOT NULL CHECK(unit_price_vnd_snapshot BETWEEN 1 AND 1000000000000),
    quantity smallint NOT NULL CHECK(quantity BETWEEN 1 AND 10),
    PRIMARY KEY(round_id,item_no)
);
CREATE TABLE IF NOT EXISTS submissions (
    round_id bigint NOT NULL,
    match_id bigint NOT NULL,
    user_id bigint NOT NULL,
    guessed_price_vnd bigint CHECK(guessed_price_vnd BETWEEN 0 AND 1000000000000),
    response_ms integer CHECK(response_ms >= 0),
    score integer NOT NULL CHECK(score BETWEEN 0 AND 1000),
    submission_status smallint NOT NULL CHECK(submission_status IN (0,1)),
    PRIMARY KEY(round_id,user_id),
    FOREIGN KEY(round_id,match_id) REFERENCES rounds(id,match_id) ON DELETE CASCADE,
    FOREIGN KEY(match_id,user_id) REFERENCES match_players(match_id,user_id) ON DELETE CASCADE,
    CHECK ((submission_status=0 AND guessed_price_vnd IS NULL AND response_ms IS NULL AND score=0) OR
           (submission_status=1 AND guessed_price_vnd IS NOT NULL AND response_ms IS NOT NULL))
);
-- One parameterized SELECT invokes this entire write atomically. Retrying the exact
-- same immutable document is a no-op; reusing an ID with another document is rejected.
CREATE OR REPLACE FUNCTION public.pr_save_match(doc jsonb) RETURNS bigint
LANGUAGE plpgsql SET search_path=public,pg_temp AS $$
DECLARE
    match_key bigint := (doc->>'id')::bigint;
    inserted_id bigint;
    round_key bigint;
    p jsonb; r jsonb; item jsonb; s jsonb;
    item_index integer;
BEGIN
    IF jsonb_typeof(doc->'players') IS DISTINCT FROM 'array' OR
       jsonb_typeof(doc->'rounds') IS DISTINCT FROM 'array' THEN
        RAISE EXCEPTION 'Invalid match document arrays';
    END IF;
    IF jsonb_array_length(doc->'players') NOT BETWEEN 2 AND 4 OR
       jsonb_array_length(doc->'rounds') NOT BETWEEN 0 AND 5 THEN
        RAISE EXCEPTION 'Invalid match document';
    END IF;
    INSERT INTO matches(id,room_code_snapshot,status,started_at,ended_at,abort_reason,snapshot)
    VALUES(match_key,doc->>'room',(doc->>'status')::smallint,
        to_timestamp((doc->>'started_ms')::numeric/1000),to_timestamp((doc->>'ended_ms')::numeric/1000),
        (doc->>'abort_reason')::smallint,doc)
    ON CONFLICT(id) DO NOTHING RETURNING id INTO inserted_id;
    IF inserted_id IS NULL THEN
        IF (SELECT snapshot FROM matches WHERE id=match_key) IS DISTINCT FROM doc THEN
            RAISE EXCEPTION 'Match ID collision';
        END IF;
        RETURN match_key;
    END IF;
    FOR p IN SELECT value FROM jsonb_array_elements(doc->'players') LOOP
        INSERT INTO match_players VALUES(match_key,(p->>'id')::bigint,p->>'name',
            (p->>'total')::integer,(p->>'rank')::smallint,(p->>'disconnected')::boolean);
    END LOOP;
    FOR r IN SELECT value FROM jsonb_array_elements(doc->'rounds') LOOP
        INSERT INTO rounds(match_id,round_no,round_type,round_state,duration_ms,actual_total_vnd)
        VALUES(match_key,(r->>'no')::smallint,(r->>'type')::smallint,(r->>'state')::smallint,
            (r->>'duration_ms')::integer,(r->>'actual_total')::bigint) RETURNING id INTO round_key;
        item_index := 0;
        FOR item IN SELECT value FROM jsonb_array_elements(r->'items') LOOP
            item_index := item_index+1;
            INSERT INTO round_items VALUES(round_key,item_index,
                (SELECT id FROM products WHERE id=(item->>'id')::bigint),
                item->>'name',item->>'image',(item->>'unit_price')::bigint,(item->>'quantity')::smallint);
        END LOOP;
        FOR s IN SELECT value FROM jsonb_array_elements(r->'submissions') LOOP
            INSERT INTO submissions VALUES(round_key,match_key,(s->>'id')::bigint,
                (s->>'guess')::bigint,(s->>'response_ms')::integer,(s->>'score')::integer,(s->>'submitted')::smallint);
        END LOOP;
    END LOOP;
    IF (doc->>'status')::integer=2 AND (SELECT count(*) FROM rounds WHERE match_id=match_key)<>5 THEN
        RAISE EXCEPTION 'Finished match requires five rounds';
    END IF;
    RETURN match_key;
END $$;
INSERT INTO schema_migrations(version) VALUES(2) ON CONFLICT DO NOTHING;
COMMIT;
