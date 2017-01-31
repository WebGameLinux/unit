
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <nxt_main.h>


static const char *nxt_socket_sockopt_name(nxt_uint_t level,
    nxt_uint_t sockopt);


nxt_socket_t
nxt_socket_create(nxt_uint_t domain, nxt_uint_t type, nxt_uint_t protocol,
    nxt_uint_t flags)
{
    nxt_socket_t  s;

#if (NXT_HAVE_SOCK_NONBLOCK)

    if (flags & NXT_NONBLOCK) {
        type |= SOCK_NONBLOCK;
    }

#endif

    s = socket(domain, type, protocol);

    if (nxt_slow_path(s == -1)) {
        nxt_thread_log_alert("socket(%ui, 0x%uXi, %ui) failed %E",
                             domain, type, protocol, nxt_socket_errno);
        return s;
    }

    nxt_thread_log_debug("socket(): %d", s);

#if !(NXT_HAVE_SOCK_NONBLOCK)

    if (flags & NXT_NONBLOCK) {
        if (nxt_slow_path(nxt_socket_nonblocking(s) != NXT_OK)) {
            nxt_socket_close(s);
            return -1;
        }
    }

#endif

    return s;
}


void
nxt_socket_close(nxt_socket_t s)
{
    if (nxt_fast_path(close(s) == 0)) {
        nxt_thread_log_debug("socket close(%d)", s);

    } else {
        nxt_thread_log_alert("socket close(%d) failed %E", s, nxt_socket_errno);
    }
}


nxt_int_t
nxt_socket_getsockopt(nxt_socket_t s, nxt_uint_t level, nxt_uint_t sockopt)
{
    int        val;
    socklen_t  len;

    len = sizeof(val);

    if (nxt_fast_path(getsockopt(s, level, sockopt, &val, &len) == 0)) {
        nxt_thread_log_debug("getsockopt(%d, %ui, %s): %d",
                             s, level,
                             nxt_socket_sockopt_name(level, sockopt), val);
        return val;
    }

    nxt_thread_log_error(NXT_LOG_CRIT, "getsockopt(%d, %ui, %s) failed %E",
                         s, level, nxt_socket_sockopt_name(level, sockopt),
                         val, nxt_socket_errno);

    return -1;
}


nxt_int_t
nxt_socket_setsockopt(nxt_socket_t s, nxt_uint_t level, nxt_uint_t sockopt,
    int val)
{
    socklen_t  len;

    len = sizeof(val);

    if (nxt_fast_path(setsockopt(s, level, sockopt, &val, len) == 0)) {
        nxt_thread_log_debug("setsockopt(%d, %ui, %s): %d",
                             s, level,
                             nxt_socket_sockopt_name(level, sockopt), val);
        return NXT_OK;
    }

    nxt_thread_log_error(NXT_LOG_CRIT, "setsockopt(%d, %ui, %s, %d) failed %E",
                         s, level, nxt_socket_sockopt_name(level, sockopt),
                         val, nxt_socket_errno);

    return NXT_ERROR;
}


static const char *
nxt_socket_sockopt_name(nxt_uint_t level, nxt_uint_t sockopt)
{
    switch (level) {

    case SOL_SOCKET:
        switch (sockopt) {

        case SO_SNDBUF:
            return "SO_SNDBUF";

        case SO_RCVBUF:
            return "SO_RCVBUF";

        case SO_REUSEADDR:
            return "SO_TYPE";

        case SO_TYPE:
            return "SO_TYPE";
        }

        break;

    case IPPROTO_TCP:
        switch (sockopt) {

        case TCP_NODELAY:
            return "TCP_NODELAY";

#ifdef TCP_DEFER_ACCEPT
        case TCP_DEFER_ACCEPT:
            return "TCP_DEFER_ACCEPT";
#endif
        }

        break;

#if (NXT_INET6 && defined IPV6_V6ONLY)
    case IPPROTO_IPV6:

        switch (sockopt) {

        case IPV6_V6ONLY:
            return "IPV6_V6ONLY";
        }

        break;
#endif

    }

    return "";
}


nxt_int_t
nxt_socket_bind(nxt_socket_t s, nxt_sockaddr_t *sa, nxt_bool_t test)
{
    nxt_err_t  err;

    nxt_thread_log_debug("bind(%d, %*s)", s, sa->text_len, sa->text);

    if (nxt_fast_path(bind(s, &sa->u.sockaddr, nxt_socklen(sa)) == 0)) {
        return NXT_OK;
    }

    err = nxt_socket_errno;

    if (err == NXT_EADDRINUSE && test) {
        return NXT_DECLINED;
    }

    nxt_thread_log_error(NXT_LOG_CRIT, "bind(%d, %*s) failed %E",
                         s, sa->text_len, sa->text, err);

    return NXT_ERROR;
}


nxt_int_t
nxt_socket_connect(nxt_socket_t s, nxt_sockaddr_t *sa)
{
    nxt_err_t   err;
    nxt_int_t   ret;
    nxt_uint_t  level;

    nxt_thread_log_debug("connect(%d, %*s)", s, sa->text_len, sa->text);

    if (connect(s, &sa->u.sockaddr, nxt_socklen(sa)) == 0) {
        return NXT_OK;
    }

    err = nxt_socket_errno;

    switch (err) {

    case NXT_EINPROGRESS:
        nxt_thread_log_debug("connect(%d) in progress", s);
        return NXT_AGAIN;

    case NXT_ECONNREFUSED:
#if (NXT_LINUX)
    case NXT_EAGAIN:
        /*
         * Linux returns EAGAIN instead of ECONNREFUSED
         * for UNIX sockets if a listen queue is full.
         */
#endif
        level = NXT_LOG_ERR;
        ret = NXT_DECLINED;
        break;

    case NXT_ECONNRESET:
    case NXT_ENETDOWN:
    case NXT_ENETUNREACH:
    case NXT_EHOSTDOWN:
    case NXT_EHOSTUNREACH:
        level = NXT_LOG_ERR;
        ret = NXT_ERROR;
        break;

    default:
        level = NXT_LOG_CRIT;
        ret = NXT_ERROR;
    }

    nxt_thread_log_error(level, "connect(%d, %*s) failed %E",
                         s, sa->text_len, sa->text, err);

    return ret;
}


void
nxt_socket_shutdown(nxt_socket_t s, nxt_uint_t how)
{
    nxt_err_t   err;
    nxt_uint_t  level;

    if (nxt_fast_path(shutdown(s, how) == 0)) {
        nxt_thread_log_debug("shutdown(%d, %ui)", s, how);
        return;
    }

    err = nxt_socket_errno;

    switch (err) {

    case NXT_ENOTCONN:
        level = NXT_LOG_INFO;
        break;

    case NXT_ECONNRESET:
    case NXT_ENETDOWN:
    case NXT_ENETUNREACH:
    case NXT_EHOSTDOWN:
    case NXT_EHOSTUNREACH:
        level = NXT_LOG_ERR;
        break;

    default:
        level = NXT_LOG_CRIT;
    }

    nxt_thread_log_error(level, "shutdown(%d, %ui) failed %E", s, how, err);
}


nxt_uint_t
nxt_socket_error_level(nxt_err_t err, nxt_socket_error_level_t level)
{
    switch (err) {

    case NXT_ECONNRESET:

        if ((level & NXT_SOCKET_ECONNRESET_IGNORE) != 0) {
            return NXT_LOG_DEBUG;
        }

        return NXT_LOG_ERR;

    case NXT_EINVAL:

        if ((level & NXT_SOCKET_EINVAL_IGNORE) != 0) {
            return NXT_LOG_DEBUG;
        }

        return NXT_LOG_ALERT;

    case NXT_EPIPE:
    case NXT_ENOTCONN:
    case NXT_ETIMEDOUT:
    case NXT_ENETDOWN:
    case NXT_ENETUNREACH:
    case NXT_EHOSTDOWN:
    case NXT_EHOSTUNREACH:

        if ((level & NXT_SOCKET_ERROR_IGNORE) != 0) {
            return NXT_LOG_INFO;
        }

        return NXT_LOG_ERR;

    default:
        return NXT_LOG_ALERT;
    }
}