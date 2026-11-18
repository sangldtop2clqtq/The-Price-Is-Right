#include "ui/network.h"
#include <errno.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct {
    GSource source;
    GPollFD poll_fd;
    pr_app *app;
} PrNetSource;

static gboolean net_source_prepare(GSource *source, gint *timeout) {
    PrNetSource *src = (PrNetSource *)source;
    pr_app *app = src->app;
    pr_connection *net = &app->state.net;

    if (net->fd < 0) {
        *timeout = -1;
        return FALSE;
    }

    /* Luôn theo dõi đọc, lỗi, đóng kết nối */
    src->poll_fd.events = G_IO_IN | G_IO_HUP | G_IO_ERR;

    /* Theo dõi ghi khi đang bắt tay TCP hoặc hàng đợi TX còn dữ liệu */
    if (net->connecting || net->tx.pending > 0) {
        src->poll_fd.events |= G_IO_OUT;
    }

    /* Tính thời hạn sớm nhất để đánh thức main loop */
    double now = pr_client_now();
    double earliest_deadline = 0.0;

    if (net->connecting) {
        earliest_deadline = net->connect_deadline;
    }

    for (size_t i = 0; i < 64; ++i) {
        if (net->pending[i].id) {
            if (earliest_deadline == 0.0 || net->pending[i].deadline < earliest_deadline) {
                earliest_deadline = net->pending[i].deadline;
            }
        }
    }

    if (earliest_deadline > 0.0) {
        double diff = earliest_deadline - now;
        if (diff <= 0.0) {
            *timeout = 0;
            return TRUE; /* Timeout đã tới, dispatch ngay */
        }
        *timeout = (gint)(diff * 1000.0) + 1;
    } else {
        *timeout = -1; /* Chờ vô hạn cho tới khi có sự kiện I/O */
    }

    return FALSE;
}

static gboolean net_source_check(GSource *source) {
    PrNetSource *src = (PrNetSource *)source;
    pr_app *app = src->app;
    pr_connection *net = &app->state.net;

    if (net->fd < 0) return TRUE;
    if (src->poll_fd.revents != 0) return TRUE;

    /* Kiểm tra timeout */
    double now = pr_client_now();
    if (net->connecting && now >= net->connect_deadline) return TRUE;

    for (size_t i = 0; i < 64; ++i) {
        if (net->pending[i].id && now >= net->pending[i].deadline) return TRUE;
    }

    return FALSE;
}

static gboolean net_source_dispatch(GSource *source, GSourceFunc callback, gpointer user_data) {
    (void)callback;
    (void)user_data;

    PrNetSource *src = (PrNetSource *)source;
    pr_app *app = src->app;
    pr_connection *net = &app->state.net;

    if (net->fd < 0) {
        app->net_source_id = 0;
        return G_SOURCE_REMOVE;
    }

    /* Chuyển đổi GPollFD revents sang fd_set tương thích pr_connection_process */
    fd_set reads, writes;
    FD_ZERO(&reads);
    FD_ZERO(&writes);

    if (src->poll_fd.revents & (G_IO_IN | G_IO_HUP | G_IO_ERR)) {
        FD_SET(net->fd, &reads);
    }
    if (src->poll_fd.revents & G_IO_OUT) {
        FD_SET(net->fd, &writes);
    }

    double now = pr_client_now();
    int was_connecting = net->connecting;

    int res = pr_connection_process(net, &reads, &writes, now);
    if (res < 0) {
        /* Lỗi kết nối hoặc timeout */
        const char *err = net->error ? net->error : "Mất kết nối với máy chủ";
        pr_app_disconnect(app, err);
        return G_SOURCE_REMOVE;
    }

    /* Nếu vừa kết nối thành công từ trạng thái connecting */
    if (was_connecting && !net->connecting) {
        app->state.conn_status = PR_CONN_CONNECTED;
        pr_app_update_conn_badge(app);
    }

    return G_SOURCE_CONTINUE;
}

static GSourceFuncs net_source_funcs = {
    .prepare = net_source_prepare,
    .check = net_source_check,
    .dispatch = net_source_dispatch,
    .finalize = NULL
};

void pr_net_source_attach(pr_app *app) {
    if (!app || app->state.net.fd < 0) return;

    /* Hủy source cũ nếu đang có */
    pr_net_source_detach(app);

    GSource *source = g_source_new(&net_source_funcs, sizeof(PrNetSource));
    PrNetSource *src = (PrNetSource *)source;
    src->app = app;
    src->poll_fd.fd = app->state.net.fd;
    src->poll_fd.events = G_IO_IN | G_IO_HUP | G_IO_ERR;
    src->poll_fd.revents = 0;

    g_source_add_poll(source, &src->poll_fd);
    app->net_source_id = g_source_attach(source, NULL);
    g_source_unref(source);
}

void pr_net_source_detach(pr_app *app) {
    if (!app) return;
    if (app->net_source_id != 0) {
        g_source_remove(app->net_source_id);
        app->net_source_id = 0;
    }
}

void pr_net_source_notify_send(pr_app *app) {
    (void)app;
    /* Đánh thức main context để prepare được gọi ngay lập tức */
    g_main_context_wakeup(NULL);
}
