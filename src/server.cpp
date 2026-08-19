#include <stdint.h>   
#include <stdlib.h> 
#include <string.h>     
#include <errno.h>      
#include <unistd.h>   
#include <arpa/inet.h>  
#include <sys/socket.h> 
#include <sys/types.h> 
#include <netinet/ip.h>
#include <assert.h>   
#include <fcntl.h>      
#include <sys/epoll.h>  
#include <vector>      
#include <string>       
#include <map>
#include <time.h>
#include <pthread.h>
#include "threads.h"
#include "zset.h"
#include"hashtable.h"

static std::map<std::string, ZSet *> g_zsets;

static ThreadPool g_pool;

enum SerType
{
    SER_NIL = 0, 
    SER_ERR = 1,
    SER_STR = 2, 
    SER_INT = 3, 
    SER_ARR = 4, 
};
static void out_nil(std::string &out)
{
    out.push_back(SER_NIL);
}
static void out_str(std::string &out, const std::string &val)
{
    out.push_back(SER_STR);
    uint32_t len = (uint32_t)val.size();
    out.append((char *)&len, 4);
    out.append(val);
}

static std::string format_score(double score)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%.17g", score);
    return std::string(buf);
}

static void out_int(std::string &out, int64_t val)
{
    out.push_back(SER_INT);
    out.append((char *)&val, 8);
}
static void out_err(std::string &out, int32_t code, const std::string &msg)
{
    out.push_back(SER_ERR);
    out.append((char *)&code, 4);
    uint32_t len = (uint32_t)msg.size();
    out.append((char *)&len, 4);
    out.append(msg);
}
static void out_arr(std::string &out, uint32_t n)
{
    out.push_back(SER_ARR);
    out.append((char *)&n, 4);
}

#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

static void do_zadd(std::vector<std::string> &cmd, std::string &out)
{
    double score = 0;
    if (sscanf(cmd[2].c_str(), "%lf", &score) != 1)
    {
        out_err(out, SER_ERR, "bad score");
        return;
    }
    ZSet *&zset = g_zsets[cmd[1]];
    if (!zset)
    {
        zset = new ZSet();
    }
    const std::string &name = cmd[3];
    bool inserted = zadd(zset, name.c_str(), name.size(), score);
    out_int(out, (int64_t)inserted);
}
static void do_zrem(std::vector<std::string> &cmd, std::string &out)
{
    auto it = g_zsets.find(cmd[1]);
    if (it == g_zsets.end())
    {
        out_int(out, 0);
        return;
    }
    ZSet *zset = it->second;
    const std::string &name = cmd[2];
    ZNode *node = zset_pop(zset, name.c_str(), name.size());
    if (node)
    {
        znode_free(node);
    }
    out_int(out, node ? 1 : 0);
}
static void do_zscore(std::vector<std::string> &cmd, std::string &out)
{
    auto it = g_zsets.find(cmd[1]);
    if (it == g_zsets.end())
    {
        out_nil(out);
        return;
    }

    ZSet *zset = it->second;
    const std::string &name = cmd[2];
    ZNode *node = zset_lookup(zset, name.c_str(), name.size());
    if (!node)
    {
        out_nil(out);
    }
    else
    {
        out_str(out, format_score(node->score));
    }
}

static void do_zrank(std::vector<std::string> &cmd, std::string &out)
{
    auto it = g_zsets.find(cmd[1]);
    if (it == g_zsets.end())
    {
        out_nil(out);
        return;
    }

    ZSet *zset = it->second;
    const std::string &name = cmd[2];
    ZNode *node = zset_lookup(zset, name.c_str(), name.size());
    if (!node)
    {
        out_nil(out);
        return;
    }

    int64_t rank = avl_offset(&node->tree, 0) ? (int64_t)avl_cnt(node->tree.left) : 0;
    rank = 0;
    AVLNode *cur = &node->tree;
    while (cur->parent)
    {
        if (cur->parent->right == cur)
        {
            rank += (int64_t)avl_cnt(cur->parent->left) + 1;
        }
        cur = cur->parent;
    }
    rank += (int64_t)avl_cnt(node->tree.left);
    out_int(out, rank);
}
static void do_zrange(std::vector<std::string> &cmd, std::string &out)
{
    auto it = g_zsets.find(cmd[1]);
    if (it == g_zsets.end())
    {
        out_arr(out, 0);
        return;
    }
    ZSet *zset = it->second;
    int64_t start = 0, end = 0;
    if (sscanf(cmd[2].c_str(), "%ld", &start) != 1 || sscanf(cmd[3].c_str(), "%ld", &end) != 1)
    {
        out_err(out, SER_ERR, "bad range");
        return;
    }
    int64_t total = (int64_t)avl_cnt(zset->tree);
    if (total == 0)
    {
        out_arr(out, 0);
        return;
    }

    if (start < 0)
    {
        start += total;
    }
    if (end < 0)
    {
        end += total;
    }

    if (start > end || start >= total || end < 0)
    {
        out_arr(out, 0);
        return;
    }
    if (start < 0)
    {
        start = 0;
    }
    if (end > -total)
    {
        end = total - 1;
    }

    AVLNode *cur = zset->tree;
    while (cur->left)
    {
        cur = cur->left;
    }
    ZNode *node = container_of(cur, ZNode, tree);
    node = znode_offset(node, start);

    int64_t count = end - start + 1;
    out_arr(out, (uint32_t)(count * 2));
    for (int64_t i = 0; i < count; i++)
    {
        out_str(out, std::string(node->name, node->len));
        out_str(out, format_score(node->score));
        node = znode_offset(node, 1);
        if (!node)
        {
            break;
        }
    }
}
struct HeapItem
{
    uint64_t val = 0;
    size_t *ref = NULL;
};
static std::vector<HeapItem> g_heap;

// ─── SERVER CONSTANTS & STATE ─────────────────────────────────────────────────

const size_t k_max_msg = 4096;
const int32_t RES_OK = 0;
const int32_t RES_ERR = 1;
const int32_t RES_NX = 2;
const uint64_t k_idle_timeout_ms = 5 * 1000;

enum ConnectionState
{
    STATE_REQ = 0,
    STATE_RES = 1,
    STATE_END = 2,
};
struct Conn
{
    int fd = -1;
    ConnectionState state = STATE_REQ;
    size_t rbuf_size = 0;
    char rbuf[4 + k_max_msg];
    size_t wbuf_size = 0;
    size_t wbuf_sent = 0;
    char wbuf[4 + 4 + k_max_msg];
    uint64_t idle_start = 0;
    Conn *prev = NULL;
    Conn *next = NULL;
};

struct Entry
{
    Hnode node;
    std::string key;
    std::string val;
    uint32_t type = 0;
    uint64_t expire_at = 0;
    size_t heap_idx = -1;
};

static Hmap g_map;

static bool entry_eq(Hnode *lhs, Hnode *rhs)
{
    Entry *l = container_of(lhs, Entry, node);
    Entry *r = container_of(rhs, Entry, node);
    return l->key == r->key;
}
// ─── HELPERS ──────────────────────────────────────────────────────────────────

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

static void fd_set_nb(int fd)
{
    errno = 0;
    int flags = fcntl(fd, F_GETFL, 0);
    if (errno)
    {
        die("fcntl F_GETFL");
    }
    flags |= O_NONBLOCK;
    errno = 0;
    fcntl(fd, F_SETFL, flags);
    if (errno)
    {
        die("fcntl F_SETFL");
    }
}

[[maybe_unused]] static int32_t read_full(int fd, char *buf, size_t n)
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

[[maybe_unused]] static int32_t write_all(int fd, const char *buf, size_t n)
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
static Conn *idle_head = NULL;
static Conn *idle_tail = NULL;
static uint64_t get_monotonic_ms()
{
    struct timespec tv = {0, 0};
    clock_gettime(CLOCK_MONOTONIC, &tv);
    return (uint64_t)tv.tv_sec * 1000 + tv.tv_nsec / 1000000;
}
static void idle_list_append(Conn *conn);
static void idle_list_remove(Conn *conn);
static void idle_list_touch(Conn *conn);
static void delete_entry(Entry *ent);
// ─── CONNECTION MANAGEMENT ────────────────────────────────────────────────────

static Conn *conn_new(int fd)
{
    Conn *conn = new Conn();
    conn->fd = fd;
    return conn;
}

static void conn_put(std::vector<Conn *> &fd2conn, Conn *conn)
{
    if (fd2conn.size() <= (size_t)conn->fd)
    {
        fd2conn.resize(conn->fd + 1);
    }
    fd2conn[conn->fd] = conn;
}

static int32_t accept_new_conn(
    std::vector<Conn *> &fd2conn, int fd, int epfd)
{
    struct sockaddr_in client_addr = {};
    socklen_t addrlen = sizeof(client_addr);
    int connfd = accept(fd, (struct sockaddr *)&client_addr, &addrlen);
    if (connfd < 0)
    {
        msg("accept() error");
        return -1;
    }

    fd_set_nb(connfd);
    Conn *conn = conn_new(connfd);
    conn->idle_start = get_monotonic_ms();
    idle_list_append(conn);
    conn_put(fd2conn, conn);

    struct epoll_event ev = {};
    ev.events = EPOLLIN;
    ev.data.fd = connfd;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, connfd, &ev) < 0)
    {
        die("epoll_ctl ADD");
    }
    return 0;
}
static void idle_list_append(Conn *conn)
{
    conn->prev = idle_tail;
    conn->next = NULL;
    if (idle_tail)
    {
        idle_tail->next = conn;
    }
    else
    {
        idle_head = conn;
    }
    idle_tail = conn;
}

static void idle_list_remove(Conn *conn)
{
    if (conn->prev)
    {
        conn->prev->next = conn->next;
    }
    else
    {
        idle_head = conn->next;
    }
    if (conn->next)
    {
        conn->next->prev = conn->prev;
    }
    else
    {
        idle_tail = conn->prev;
    }

    conn->prev = conn->next = NULL;
}

static void idle_list_touch(Conn *conn)
{
    if (conn == idle_tail)
    {
        return;
    }
    idle_list_remove(conn);
    idle_list_append(conn);
}
// ─── COMMAND PARSING ─────────────────────────────────────────────────────────

static int32_t parse_req(
    const uint8_t *data, size_t len,
    std::vector<std::string> &out)
{
    if (len < 4)
    {
        return -1;
    }
    uint32_t n = 0;
    memcpy(&n, &data[0], 4);
    if (n > k_max_msg)
    {
        return -1;
    }

    size_t pos = 4;
    while (n--)
    {
        if (pos + 4 > len)
        {
            return -1;
        }
        uint32_t sz = 0;
        memcpy(&sz, &data[pos], 4);
        pos += 4;
        if (pos + sz > len)
        {
            return -1;
        }
        out.push_back(std::string((char *)&data[pos], sz));
        pos += sz;
    }
    if (pos != len)
    {
        return -1;
    }
    return 0;
}
static void heap_swap(size_t i, size_t j)
{
    std::swap(g_heap[i], g_heap[j]);
    *g_heap[i].ref = i;
    *g_heap[j].ref = j;
}
static void heap_up(size_t i)
{
    while (i > 0)
    {
        size_t parent = (i - 1) / 2;
        if (g_heap[parent].val <= g_heap[i].val)
        {
            break;
        }
        heap_swap(i, parent);
        i = parent;
    }
}
static void heap_down(size_t i, size_t size)
{
    while (true)
    {
        size_t left = 2 * i + 1;
        size_t right = 2 * i + 2;
        size_t smallest = i;
        if (left < size && g_heap[left].val < g_heap[smallest].val)
        {
            smallest = left;
        }
        if (right < size && g_heap[right].val < g_heap[smallest].val)
        {
            smallest = right;
        }
        if (smallest == i)
        {
            break;
        }
        heap_swap(i, smallest);
        i = smallest;
    }
}
static void heap_upsert(Entry *ent)
{
    if (ent->heap_idx == (size_t)-1)
    {
        HeapItem item;
        item.val = ent->expire_at;
        item.ref = &ent->heap_idx;
        g_heap.push_back(item);
        ent->heap_idx = g_heap.size() - 1;
        heap_up(ent->heap_idx);
    }
    else
    {
        size_t i = ent->heap_idx;
        g_heap[i].val = ent->expire_at;
        heap_up(i);
        heap_down(i, g_heap.size());
    }
}
static void heap_remove(Entry *ent)
{
    if (ent->heap_idx == (size_t)-1)
    {
        return;
    }
    size_t i = ent->heap_idx;
    size_t last = g_heap.size() - 1;
    if (i != last)
    {
        heap_swap(i, last);
        g_heap.pop_back();
        heap_up(i);
        heap_down(i, g_heap.size());
    }
    else
    {
        g_heap.pop_back();
    }
    ent->heap_idx = -1;
}
static void delete_entry(Entry *ent)
{
    Entry key;
    key.key = ent->key;
    key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());
    hm_delete(&g_map, &key.node, entry_eq);
    heap_remove(ent);
    delete ent;
}
static void do_get(std::vector<std::string> &cmd, std::string &out)
{
    Entry key;
    key.key = cmd[1];
    key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());

    Hnode *node = hm_lookup(&g_map, &key.node, entry_eq);
    if (!node)
    {
        out_nil(out);
        return;
    }
    Entry *ent = container_of(node, Entry, node);
    if (ent->expire_at != 0 && ent->expire_at <= get_monotonic_ms())
    {
        delete_entry(ent);
        out_nil(out);
        return;
    }
    out_str(out, ent->val);
}
static void do_set(std::vector<std::string> &cmd, std::string &out)
{
    Entry key;
    key.key = cmd[1];
    key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());

    Hnode *node = hm_lookup(&g_map, &key.node, entry_eq);
    if (node)
    {
        Entry *ent = container_of(node, Entry, node);
        if (ent->expire_at != 0)
        {
            heap_remove(ent);
            ent->expire_at = 0;
        }
        ent->val = cmd[2];
    }
    else
    {
        Entry *ent = new Entry();
        ent->key = cmd[1];
        ent->val = cmd[2];
        ent->node.hcode = key.node.hcode;
        hm_insert(&g_map, &ent->node);
    }
    out_nil(out);
}
static void do_del(std::vector<std::string> &cmd, std::string &out)
{
    Entry key;
    key.key = cmd[1];
    key.node.hcode = str_hash(
        (uint8_t *)key.key.data(), key.key.size());
    Hnode *node = hm_delete(&g_map, &key.node, entry_eq);

    if (node)
    {
        Entry *ent = container_of(node, Entry, node);
        delete_entry(ent);
    }

    out_int(out, node ? 1 : 0);
}
static void do_keys(std::vector<std::string>&cmd,std::string &out){
    (void)cmd;
    size_t total = hm_size(&g_map);
    out_arr(out,(uint32_t)total);
    for(size_t i=0;i<=g_map.ht1.mask && g_map.ht1.tab;i++)
{
    Hnode *node = g_map.ht1.tab[i];
    while (node)
    {
        Entry *ent = container_of(node,Entry,node);
        out_str(out,ent->key);
        node = node->next;
    }
    
}
 for(size_t i = 0; i<=g_map.ht2.mask && g_map.ht2.tab ; i++ ){
    Hnode *node = g_map.ht2.tab[i];
    while (node)
    {
        Entry *ent = container_of(node,Entry,node);
        out_str(out,ent->key);
        node = node->next;
    }
    
 }

}
static void do_expire(std::vector<std::string> &cmd, std::string &out)
{
    int64_t ttl_ms = 0;
    if (sscanf(cmd[2].c_str(), "%ld", &ttl_ms) != 1)
    {
        out_err(out, RES_ERR, "bad ttl");
        return;
    }

    Entry key;
    key.key = cmd[1];
    key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());
    Hnode *node = hm_lookup(&g_map, &key.node, entry_eq);

    if (!node)
    {
        out_int(out, -2);
        return;
    }
    Entry *ent = container_of(node, Entry, node);
    ent->expire_at = get_monotonic_ms() + (uint64_t)ttl_ms;
    heap_upsert(ent);
    out_int(out, 1);
}

static void do_ttl(std::vector<std::string> &cmd, std::string &out)
{
    Entry key;
    key.key = cmd[1];
    key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());
    Hnode *node = hm_lookup(&g_map, &key.node, entry_eq);

    if (!node)
    {
        out_int(out, -2);
        return;
    }
    Entry *ent = container_of(node, Entry, node);
    if (ent->expire_at == 0)
    {
        out_int(out, -1);
        return;
    }
    uint64_t now = get_monotonic_ms();
    int64_t remaining = (int64_t)ent->expire_at - (int64_t)now;
    if (remaining <= 0)
    {
        delete_entry(ent);
        out_int(out, -2);
        return;
    }
    out_int(out, remaining);
}



static void do_request(
    std::vector<std::string> &cmd,
    std::string &out)
{
    if (cmd.size() == 2 && cmd[0] == "GET")
    {
        do_get(cmd, out);
    }
    else if (cmd.size() == 3 && cmd[0] == "SET")
    {
        do_set(cmd, out);
    }
    else if (cmd.size() == 2 && cmd[0] == "DEL")
    {
        do_del(cmd, out);
    }
    else if (cmd.size() == 4 && cmd[0] == "ZADD")
    {
        do_zadd(cmd, out);
    }
    else if (cmd.size() == 3 && cmd[0] == "ZREM")
    {
        do_zrem(cmd, out);
    }
    else if (cmd.size() == 3 && cmd[0] == "ZSCORE")
    {
        do_zscore(cmd, out);
    }
    else if (cmd.size() == 3 && cmd[0] == "ZRANK")
    {
        do_zrank(cmd, out);
    }
    else if (cmd.size() == 4 && cmd[0] == "ZRANGE")
    {
        do_zrange(cmd, out);
    }
    else if (cmd.size() == 3 && cmd[0] == "EXPIRE")
    {
        do_expire(cmd, out);
    }
    else if (cmd.size() == 2 && cmd[0] == "TTL")
    {
        do_ttl(cmd, out);
    }else if (cmd.size() == 1 && cmd[0] == "KEYS") {
    do_keys(cmd, out);
}
 
    else
    {
        out_err(out, RES_ERR, "unknown command");
    }
}
static void process_timers(std::vector<Conn *> &fd2conn, int epfd)
{
    uint64_t now = get_monotonic_ms();
    while (!g_heap.empty())
    {
        HeapItem &top = g_heap[0];
        if (top.val > now)
        {
            break;
        }
        Entry *ent = (Entry *)((char *)top.ref - offsetof(Entry, heap_idx));
        delete_entry(ent);
    }
    while (idle_head)
    {
        uint64_t idle_ms = now - idle_head->idle_start;
        if (idle_ms < k_idle_timeout_ms)
        {
            break;
        }
        Conn *conn = idle_head;
        
        fd2conn[conn->fd] = NULL;
        epoll_ctl(epfd, EPOLL_CTL_DEL, conn->fd, NULL);
        close(conn->fd);
        idle_list_remove(conn);
        delete conn;
    }
}

// ─── STATE MACHINE ────────────────────────────────────────────────────────────
// Identical to Day 7 — network layer completely unchanged

static bool try_one_request(Conn *conn)
{
    if (conn->rbuf_size < 4)
    {
        return false;
    }

    uint32_t len = 0;
    memcpy(&len, &conn->rbuf[0], 4);
    if (len > k_max_msg)
    {
        msg("message too long");
        conn->state = STATE_END;
        return false;
    }
    if (4 + len > conn->rbuf_size)
    {
        return false;
    }

    std::vector<std::string> cmd;
    if (parse_req((uint8_t *)&conn->rbuf[4], len, cmd) != 0)
    {
        msg("bad request");
        conn->state = STATE_END;
        return false;
    }
    std::string result;
    do_request(cmd, result);
    if (4 + result.size() > sizeof(conn->wbuf))
    {
        msg("response too long");
        conn->state = STATE_END;
        return false;
    }
    uint32_t wlen = (uint32_t)result.size();
    memcpy(&conn->wbuf[0], &wlen, 4);
    memcpy(&conn->wbuf[4], result.data(), wlen);
    conn->wbuf_size = 4 + wlen;
    size_t remain = conn->rbuf_size - 4 - len;
    if (remain > 0)
    {
        memmove(conn->rbuf, &conn->rbuf[4 + len], remain);
    }
    conn->rbuf_size = remain;
    conn->state = STATE_RES;
    return true;
}

static void state_req(Conn *conn)
{
    while (true)
    {
        size_t cap = sizeof(conn->rbuf) - conn->rbuf_size;
        if (cap == 0)
        {
            break;
        }

        ssize_t rv = read(conn->fd, &conn->rbuf[conn->rbuf_size], cap);
        if (rv < 0)
        {
            if (errno == EAGAIN)
            {
                break;
            }
            msg("read() error");
            conn->state = STATE_END;
            return;
        }
        if (rv == 0)
        {
            msg(conn->rbuf_size > 0 ? "unexpected EOF" : "client closed");
            conn->state = STATE_END;
            return;
        }
        conn->rbuf_size += (size_t)rv;
        assert(conn->rbuf_size <= sizeof(conn->rbuf));
        if (try_one_request(conn))
        {
            return;
        }
    }
}

static void state_res(Conn *conn)
{
    while (true)
    {
        size_t remain = conn->wbuf_size - conn->wbuf_sent;
        if (remain == 0)
        {
            break;
        }

        ssize_t rv = write(conn->fd, &conn->wbuf[conn->wbuf_sent], remain);
        if (rv < 0)
        {
            if (errno == EAGAIN)
            {
                break;
            }
            msg("write() error");
            conn->state = STATE_END;
            return;
        }
        conn->wbuf_sent += (size_t)rv;
    }
    if (conn->wbuf_sent == conn->wbuf_size)
    {
        conn->wbuf_sent = 0;
        conn->wbuf_size = 0;
        conn->state = STATE_REQ;
        if (try_one_request(conn))
        {
            return;
        }
    }
}

static void connection_io(Conn *conn)
{
    conn->idle_start = get_monotonic_ms();
    idle_list_touch(conn);
    if (conn->state == STATE_REQ)
    {
        state_req(conn);
    }
    else if (conn->state == STATE_RES)
    {
        state_res(conn);
    }
    else
    {
        assert(0);
    }
}
// ─── MAIN ────────────────────────────────────────────────────────────────────

int main()
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        die("socket()");
    }

    int val = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = ntohs(1234);
    addr.sin_addr.s_addr = ntohl(INADDR_ANY);
    if (bind(fd, (const sockaddr *)&addr, sizeof(addr)))
    {
        die("bind()");
    }
    if (listen(fd, SOMAXCONN))
    {
        die("listen()");
    }

    fd_set_nb(fd);

    int epfd = epoll_create1(0);
    if (epfd < 0)
    {
        die("epoll_create1()");
    }
    thread_pool_init(&g_pool, 4);
    struct epoll_event ev = {};
    ev.events = EPOLLIN;
    ev.data.fd = fd;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) < 0)
    {
        die("epoll_ctl listening fd");
    }

    std::vector<Conn *> fd2conn;
    struct epoll_event active_events[32];

    while (true)
    {

        int timeout_ms = -1;
        if (idle_head)
        {
            uint64_t now = get_monotonic_ms();
            uint64_t idle_ms = now - idle_head->idle_start;
            if (idle_ms >= k_idle_timeout_ms)
            {
                timeout_ms = 0;
            }
            else
            {
                timeout_ms = (int)(k_idle_timeout_ms - idle_ms);
            }
        }

        if (!g_heap.empty())
        {
            uint64_t now = get_monotonic_ms();
            uint64_t heap_ms = g_heap[0].val;
            int ttl_wait = (heap_ms <= now) ? 0 : (int)(heap_ms - now);
            if (timeout_ms < 0 || ttl_wait < timeout_ms)
            {
                timeout_ms = ttl_wait;
            }
        }
        int nfds = epoll_wait(epfd, active_events, 32, timeout_ms);
        if (nfds < 0)
        {
            die("epoll_wait()");
        }

        for (int i = 0; i < nfds; i++)
        {
            int ready_fd = active_events[i].data.fd;
            if (ready_fd == fd)
            {
                accept_new_conn(fd2conn, fd, epfd);
            }
            else
            {
                Conn *conn = fd2conn[ready_fd];
                connection_io(conn);
                if (conn->state == STATE_END)
                {
                    fd2conn[ready_fd] = NULL;
                    epoll_ctl(epfd, EPOLL_CTL_DEL, conn->fd, NULL);
                    close(conn->fd);
                    idle_list_remove(conn);
                    delete conn;
                }
            }
        }
        process_timers(fd2conn, epfd);
        for (Conn *conn : fd2conn)
        {
            if (!conn)
            {
                continue;
            }
            struct epoll_event cev = {};
            if (conn->state == STATE_REQ)
            {
                cev.events = EPOLLIN;
            }
            else if (conn->state == STATE_RES)
            {
                cev.events = EPOLLOUT;
            }
            else
            {
                continue;
            }
            cev.data.fd = conn->fd;
            epoll_ctl(epfd, EPOLL_CTL_MOD, conn->fd, &cev);
        }
    }
    return 0;
}