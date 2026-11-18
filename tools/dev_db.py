"""Dedicated local development PostgreSQL cluster; never touches a system service."""
import argparse
import os
from pathlib import Path
import shutil
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]
LOCAL = ROOT / ".local"
DATA = LOCAL / "postgres"
SOCKETS = LOCAL / "socket"
DEFAULT_URL = "hostaddr=127.0.0.1 port=55432 dbname=price_game user=price_game connect_timeout=5"


def pg_binary(name):
    directories = [os.environ.get("PR_PG_BIN", ""), "/opt/homebrew/opt/postgresql@18/bin",
                   "/usr/local/opt/postgresql@18/bin"]
    # Debian/Ubuntu versioned binary directories are not normally on PATH.
    directories.extend(str(p) for p in sorted(Path("/usr/lib/postgresql").glob("*/bin"), reverse=True))
    for directory in directories:
        if directory and (Path(directory) / name).is_file():
            return str(Path(directory) / name)
    found = shutil.which(name)
    if not found:
        raise RuntimeError(f"Missing PostgreSQL executable: {name}; install PostgreSQL or set PR_PG_BIN")
    return found


def sql(conninfo, query=None, file=None):
    args = [pg_binary("psql"), "-X", "-v", "ON_ERROR_STOP=1", "-d", conninfo]
    if query is not None:
        args += ["-At", "-c", query]
    if file is not None:
        args += ["-f", str(file)]
    result = subprocess.run(args, capture_output=True, text=True)
    if result.returncode:
        # Do not put connection credentials into a CalledProcessError command dump.
        raise RuntimeError("PostgreSQL operation failed: " + result.stderr.strip())
    return result.stdout


def apply_migrations(conninfo):
    for path in sorted((ROOT / "db/migrations").glob("*.sql")):
        sql(conninfo, file=path)


def apply_seeds(conninfo):
    for path in sorted((ROOT / "db/seeds").glob("*.sql")):
        sql(conninfo, file=path)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=["start", "stop", "status", "migrate", "seed"])
    args = parser.parse_args()
    conninfo = os.environ.get("PR_DATABASE_URL", DEFAULT_URL)
    if args.command in ("migrate", "seed"):
        (apply_migrations if args.command == "migrate" else apply_seeds)(conninfo)
        print(f"Database {args.command}: OK")
        return
    pg_ctl = pg_binary("pg_ctl")
    if args.command in ("stop", "status"):
        if not (DATA / "PG_VERSION").exists():
            print("Development database has not been initialized")
            return
        command = [pg_ctl, "-D", str(DATA), args.command]
        if args.command == "stop":
            command += ["-m", "fast", "-w"]
        subprocess.run(command, check=True)
        return
    LOCAL.mkdir(exist_ok=True)
    SOCKETS.mkdir(exist_ok=True)
    if not (DATA / "PG_VERSION").exists():
        subprocess.run([pg_binary("initdb"), "-D", str(DATA), "-U", "price_game",
                        "--auth=trust", "--encoding=UTF8", "--locale=C"], check=True,
                       stdout=subprocess.DEVNULL)
    running = subprocess.run([pg_ctl, "-D", str(DATA), "status"], capture_output=True).returncode == 0
    if not running:
        subprocess.run([pg_ctl, "-D", str(DATA), "-l", str(LOCAL / "postgres.log"),
                        "-o", shlex.join(["-h", "127.0.0.1", "-p", "55432", "-k", str(SOCKETS)]),
                        "-w", "start"], check=True)
    admin_url = DEFAULT_URL.replace("dbname=price_game", "dbname=postgres")
    if sql(admin_url, "SELECT 1 FROM pg_database WHERE datname='price_game'").strip() != "1":
        sql(admin_url, "CREATE DATABASE price_game ENCODING 'UTF8' TEMPLATE template0")
    # This command always prepares the dedicated local cluster, independent of env overrides.
    apply_migrations(DEFAULT_URL)
    apply_seeds(DEFAULT_URL)
    print("Development DB ready: 127.0.0.1:55432 / price_game (loopback-only trust authentication)")
    print("Run: PR_DATABASE_URL='" + DEFAULT_URL + "' ./build/pr_server")


if __name__ == "__main__":
    main()
