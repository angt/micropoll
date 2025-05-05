#include <errno.h>
#include <poll.h>

#ifndef MICROPOLL_MAX_FD
#define MICROPOLL_MAX_FD 8
#endif

#if MICROPOLL_MAX_FD <= 0
#error "MICROPOLL_MAX_FD <= 0"
#endif

#ifndef MICROPOLL_MAX_POLLOUT
#define MICROPOLL_MAX_POLLOUT 4
#endif

#if MICROPOLL_MAX_POLLOUT <= 0
#error "MICROPOLL_MAX_POLLOUT <= 0"
#endif

#define MICROPOLL_FUNCTION __attribute__((unused)) static

struct micropoll;

struct micropoll_cb {
    int (*fn)(struct micropoll *, int, void *);
    void *data;
};

struct micropoll_fd {
    int set;
    int fd;
    struct micropoll_cb pollin;
    struct micropoll_cb pollerr;
    struct micropoll_cb pollout[MICROPOLL_MAX_POLLOUT];
};

struct micropoll {
    struct micropoll_fd fd[MICROPOLL_MAX_FD];
};

MICROPOLL_FUNCTION struct micropoll_cb
micropoll_cb(int (*fn)(struct micropoll *, int, void *), void *data)
{
    return (struct micropoll_cb){
        .fn = fn,
        .data = data
    };
}

MICROPOLL_FUNCTION int
micropoll_retry(int ret)
{
    return (ret == -1) && (errno == EAGAIN || errno == EWOULDBLOCK ||
                           errno == EINTR  || errno == ENOBUFS);
}

MICROPOLL_FUNCTION int
micropoll_set_cb(struct micropoll_fd *mfd, short event, struct micropoll_cb cb)
{
    if (!mfd || !event)
        return -1;

    if (event == POLLIN) {
        mfd->pollin = cb;
        return 0;
    }
    if (event == POLLERR) {
        mfd->pollerr = cb;
        return 0;
    }
    if (event != POLLOUT) {
        errno = EINVAL;
        return -1;
    }
    int empty = -1;

    for (int i = 0; i < MICROPOLL_MAX_POLLOUT; i++) {
        if (mfd->pollout[i].fn == cb.fn && mfd->pollout[i].data == cb.data) {
            return 0;
        } else if (empty == -1) {
            empty = i;
        }
    }
    if (empty == -1) {
        errno = ENOBUFS;
        return -1;
    }
    mfd->pollout[empty] = cb;
    return 0;
}

MICROPOLL_FUNCTION int
micropoll_set(struct micropoll *mp, int fd, short event,
              int (*fn)(struct micropoll *, int, void *), void *data)
{
    struct micropoll_cb cb = micropoll_cb(fn, data);

    if (fd < 0) {
        errno = EINVAL;
        return -1;
    }
    int empty = -1;

    for (int i = 0; i < MICROPOLL_MAX_FD; i++) {
        if (mp->fd[i].set) {
            if (mp->fd[i].fd == fd)
                return micropoll_set_cb(&mp->fd[i], event, cb);
        } else if (empty == -1) {
            empty = i;
        }
    }
    if (empty == -1) {
        errno = ENOBUFS;
        return -1;
    }
    mp->fd[empty] = (struct micropoll_fd) {
        .set = 1,
        .fd = fd,
    };
    return micropoll_set_cb(&mp->fd[empty], event, cb);
}

MICROPOLL_FUNCTION int
micropoll(struct micropoll *mp, int timeout)
{
    struct pollfd pfd[MICROPOLL_MAX_FD] = {{0}};
    int map[MICROPOLL_MAX_FD];
    int n = 0;

    for (int i = 0; i < MICROPOLL_MAX_FD; i++) {
        if (!mp->fd[i].set || mp->fd[i].fd < 0)
            continue;

        struct micropoll_fd *mfd = &mp->fd[i];

        struct pollfd fd = {
            .fd = mfd->fd
        };
        if (mfd->pollin.fn)
            fd.events |= POLLIN;

        if (mfd->pollout[0].fn)
            fd.events |= POLLOUT;

        pfd[n] = fd;
        map[n] = i;
        n++;
    }
    if (!n)
        return -2;

    int ret = poll(pfd, n, timeout);

    if (ret <= 0)
        return ret;

    for (int i = 0; i < n; ++i) {
        if (pfd[i].revents == 0)
            continue;

        struct micropoll_fd *mfd = &mp->fd[map[i]];

        if (!mfd->set || mfd->fd != pfd[i].fd)
             continue;

        short revents = pfd[i].revents;

        if (revents & (POLLERR | POLLNVAL)) {
            if (mfd->pollerr.fn)
                mfd->pollerr.fn(mp, mfd->fd, mfd->pollerr.data);
            mfd->set = 0;
            continue;
        }
        if (mfd->pollin.fn && (revents & POLLIN)) {
            if (!mfd->pollin.fn(mp, mfd->fd, mfd->pollin.data))
                mfd->pollin = (struct micropoll_cb){0};
        }
        if (mfd->pollout[0].fn && (revents & POLLOUT)) {
            if (!mfd->pollout[0].fn(mp, mfd->fd, mfd->pollout[0].data)) {
                for (int k = 1; k < MICROPOLL_MAX_POLLOUT; k++)
                    mfd->pollout[k - 1] = mfd->pollout[k];
                mfd->pollout[MICROPOLL_MAX_POLLOUT - 1] = (struct micropoll_cb){0};
            }
        }
    }
    return 0;
}

MICROPOLL_FUNCTION int
micropoll_set_in(struct micropoll *mp, int fd,
                 int (*fn)(struct micropoll *, int, void *), void *data)
{
    return micropoll_set(mp, fd, POLLIN, fn, data);
}

MICROPOLL_FUNCTION int
micropoll_set_out(struct micropoll *mp, int fd,
                  int (*fn)(struct micropoll *, int, void *), void *data)
{
    return micropoll_set(mp, fd, POLLOUT, fn, data);
}

MICROPOLL_FUNCTION int
micropoll_set_err(struct micropoll *mp, int fd,
                  int (*fn)(struct micropoll *, int, void *), void *data)
{
    return micropoll_set(mp, fd, POLLERR, fn, data);
}
