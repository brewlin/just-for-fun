#include "http.h"
#include "connection.h"
#include "cycle.h"
#include "request.h"
#include "event.h"
#include "log.h"

static int_t  http_process_init(cycle_t * cycle);
static listening_t *http_add_listening(cycle_t *cycle, http_listen_opt_t *opt);
static void http_init_request(event_t *rev);
static void http_init_connection(connection_t *c);
static void http_empty_handler(event_t *wev);
void http_close_connection(connection_t *c);

module_t  http_core_module = {
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    http_process_init,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
};

//这里其实应该是核心http模块的启动，主要是启动监听端口
//但是端口配置添加 listening_t 在nginx中是通过nginx.conf配置解析时添加的
//我们这里作为演示就直接放到http core模块启动方法中
static int_t http_process_init(cycle_t *cycle)
{
    log_info(cycle->log,"http: process init");
    http_listen_opt_t   lsopt;
    struct sockaddr_in serv_addr;   
    memzero(&lsopt, sizeof(http_listen_opt_t));
    memzero(&serv_addr,sizeof(serv_addr));
    // listen 127.0.0.1:8000;
    // listen 127.0.0.1 不加端口，默认监听80端口;
    // listen 8000
    // listen *:8000
    // listen localhost:8000

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(8089);

    lsopt.sockaddr = (struct sockaddr *)&serv_addr;
    lsopt.socklen = sizeof(serv_addr);

    lsopt.backlog = BACKLOG;
    lsopt.rcvbuf = -1;
    lsopt.sndbuf = -1;

    listening_t           *ls;
    ls = http_add_listening(cycle, &lsopt);
    if (ls == NULL) {
        return ERROR;
    }
    //初始化对应socket
    return open_listening_sockets(cycle);
}


static listening_t *http_add_listening(cycle_t *cycle, http_listen_opt_t *opt)
{
    listening_t           *ls;

    ls = create_listening(cycle, opt->sockaddr, opt->socklen);
    if (ls == NULL) {
        return NULL;
    }

    ls->handler = http_init_connection;

    ls->pool_size = DEFAULT_POOL_SIZE;
    // ls->post_accept_timeout = 1;

    ls->backlog = opt->backlog;
    ls->rcvbuf = opt->rcvbuf;
    ls->sndbuf = opt->sndbuf;
    ls->root = "/tmp/www";


    return ls;
}

static void http_init_connection(connection_t *c)
{
    event_t         *rev;
    rev = c->read;
    rev->handler = http_init_request;
    c->write->handler = http_empty_handler;

    if (rev->ready) {
        rev->handler(rev);
        return;
    }

    if (handle_read_event(rev, 0) != OK) {
        http_close_connection(c);
        return;
    }
}


static void http_init_request(event_t *rev)
{
    connection_t           *c;
    http_connection         *hc;
    c = rev->data;
    hc = c->data;
    if (hc == NULL) {
        hc = pcalloc(c->pool, sizeof(http_connection));
        if (hc == NULL) {
            http_close_connection(c);
            return;
        }
    }
    c->data = hc;
    hc->connection = c;
    hc->log = c->log;

    connection_init(hc);
    rev->handler = connection_handler;
    // connection_handler(hc);
    // connection_close(hc);

    // http_close_connection(c);
    rev->handler(rev);
}


void http_empty_handler(event_t *wev)
{
    log_info(wev->log,"http empty handler");

    return;
}
 
/**
 * 关闭释放连接
 */ 
void http_close_connection(connection_t *c)
{
    pool_t  *pool;

    log_error(c->log,"close http connection: %d", c->fd);
    c->destroyed = 1;

    pool = c->pool;

    close_connection(c);

    destroy_pool(pool);
}