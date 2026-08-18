#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/ip.h>
#include <assert.h>
#include <string>
#include <vector>
enum SerType
{
    SER_NIL = 0,
    SER_ERR = 1,
    SER_STR = 2,
    SER_INT = 3,
    SER_ARR = 4,
};
const size_t k_max_msg = 4096;

static void die(const char *msg)
{
    int err = errno;
    fprintf(stderr, "[%d] %s\n", err, msg);
    abort();
}

static void msg(const char *msg)
{
    fprintf(stderr, "%s\n", msg);
}

static int32_t read_full(int fd, char *buf, size_t n)
{
    while (n > 0)
    {
        ssize_t rv = read(fd, buf, n);
        if (rv <= 0)
        {
            return -1;
        }
        assert((size_t)rv <= n);
        n -= (size_t)rv;
        buf += rv;
    }
    return 0;
}

static int32_t write_all(int fd, const char *buf, size_t n)
{
    while (n > 0)
    {
        ssize_t rv = write(fd, buf, n);
        if (rv <= 0)
        {
            return -1;
        }
        assert((size_t)rv <= n);
        n -= (size_t)rv;
        buf += rv;
    }
    return 0;
}

static int32_t send_req(int fd, const std::vector<std::string> &cmd)
{
    
    uint32_t len = 4;
    for (const std::string &s : cmd)
    {
        len += 4 + (uint32_t)s.size();
    }
    if (len > k_max_msg)
    {
        return -1;
    }

    char wbuf[4 + k_max_msg];

  
    memcpy(&wbuf[0], &len, 4);

 
    uint32_t n = (uint32_t)cmd.size();
    memcpy(&wbuf[4], &n, 4);

    
    size_t cur = 8;
    for (const std::string &s : cmd)
    {
        uint32_t p = (uint32_t)s.size();
        memcpy(&wbuf[cur], &p, 4);
        cur += 4;
        memcpy(&wbuf[cur], s.data(), s.size());
        cur += s.size();
    }

   
}

static int32_t parse_res(const uint8_t *data, size_t size)
{
    if (size < 1)
    {
        msg("response too short");
        return -1;
    }

    uint8_t type = data[0];
    switch (type)
    {
    case SER_NIL:
        printf("(nil)\n");
        return 1;
    case SER_STR:
        if (size < 1 + 4)
        {
            return -1;
        }
        {
            uint32_t len = 0;
            memcpy(&len, &data[1], 4);
            if (size < 1 + 4 + len)
            {
                return -1;
            }
            printf("(str) %.*s\n", (int)len, &data[1 + 4]);
            return 1 + 4 + len;
        }
    case SER_INT:
        if (size < 1 + 8)
        {
            return -1;
        }
        {
            int64_t val = 0;
            memcpy(&val, &data[1], 8);
            printf("(int) %ld\n", val);
            return 1 + 8;
        }
    case SER_ERR:
        if (size < 1 + 4 + 4)
        {
            return -1;
        }
        {
            int32_t code = 0;
            uint32_t msglen = 0;
            memcpy(&code, &data[1], 4);
            memcpy(&msglen, &data[1 + 4], 4);
            if (size < 1 + 4 + 4 + msglen)
            {
                return -1;
            }
            printf("(err) code=%d msg=%.*s\n", code, (int)msglen, &data[1 + 4 + 4]);
            return 1 + 4 + 4 + (int32_t)msglen;
        }
    case SER_ARR:
        if (size < 1 + 4)
        {
            return -1;
        }
        {
            uint32_t count = 0;
            memcpy(&count, &data[1], 4);
            printf("(arr) len=%u\n", count);
            size_t consumed = 1 + 4;
            for (uint32_t i = 0; i < count; i++)
            {
                int32_t rv = parse_res(data + consumed, size - consumed);
                if (rv < 0)
                {
                    return rv;
                }
                consumed += (size_t)rv;
            }
            printf("(arr) end\n");
            return (int32_t)consumed;
        }
    default:
        msg("unknown type tag");
        return -1;
    }
}

static int32_t read_res(int fd)
{
    char rbuf[4 + k_max_msg + 1];
    errno = 0;
    int32_t err = read_full(fd, rbuf, 4);
    if (err)
    {
        if (errno == 0)
        {
            msg("EOF");
        }
        else
        {
            msg("read() error");
        }
        return err;
    }
    uint32_t len = 0;
    memcpy(&len, &rbuf[0], 4);
    if (len > k_max_msg)
    {
        msg("response too long");
        return -1;
    }
    err = read_full(fd, &rbuf[4], len);
    if (err)
    {
        msg("read() error");
        return err;
    }

    int32_t rv = parse_res((uint8_t *)&rbuf[4], len);
    if (rv < 0)
    {
        msg("bad response");
        return -1;
    }
    return 0;
}
static void cmd(int fd, std::vector<std::string> args)
{
    if (send_req(fd, args))
    {
        fprintf(stderr, "send failed\n");
    }
    if (read_res(fd))
    {
        fprintf(stderr, "read failed\n");
    }
}
int main()
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        die("socket()");
    }

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = ntohs(1234);
    addr.sin_addr.s_addr = ntohl(INADDR_LOOPBACK);
    int rv = connect(fd, (const struct sockaddr *)&addr, sizeof(addr));
    if (rv)
    {
        die("connect()");
    }

    printf("=== KV Store ===\n");
    cmd(fd, {"SET", "name", "Alice"});
    cmd(fd, {"SET", "city", "Mumbai"});
    cmd(fd, {"SET", "lang", "C++"});
    cmd(fd, {"GET", "name"});
    cmd(fd, {"GET", "missing"});
    cmd(fd, {"DEL", "city"});
    cmd(fd, {"GET", "city"});
    cmd(fd, {"KEYS"});

    printf("\n=== TTL / Expiry ===\n");
    cmd(fd, {"SET", "temp", "value"});
    cmd(fd, {"EXPIRE", "temp", "2000"});
    cmd(fd, {"TTL", "temp"});
    cmd(fd, {"GET", "temp"});
    printf("waiting 3 seconds...\n");
    sleep(3);
    cmd(fd, {"GET", "temp"}); 
    cmd(fd, {"TTL", "temp"}); 

    printf("\n=== Sorted Set ===\n");
    cmd(fd, {"ZADD", "scores", "100", "Alice"});
    cmd(fd, {"ZADD", "scores", "200", "Bob"});
    cmd(fd, {"ZADD", "scores", "150", "Charlie"});
    cmd(fd, {"ZSCORE", "scores", "Alice"});
    cmd(fd, {"ZRANK", "scores", "Bob"});
    cmd(fd, {"ZRANGE", "scores", "0", "-1"});
    cmd(fd, {"ZREM", "scores", "Charlie"});
    cmd(fd, {"ZRANGE", "scores", "0", "-1"});

    close(fd);
    return 0;
}
