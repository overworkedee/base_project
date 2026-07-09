/**
 * cmd_subscription.c -- 数据流订阅管理实现
 *
 * 内部用链表存储 (data_id + 订阅者列表) 的二维结构。
 * 线程安全：所有公开 API 内部持有互斥锁。
 */

#define _GNU_SOURCE
#include "cmd/cmd_subscription.h"
#include "cmd/cmd_server.h"
#include "cmd/cmd_protocol.h"
#include "log/log.h"
#include "hw/hw_mutex.h"

#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>  /* htons */

/* ── 订阅者节点 ─────────────────────────────────────────────────────── */

typedef struct sub_node {
    cmd_conn_t*       conn;
    uint32_t          interval_ms;
    struct sub_node*  next;
} sub_node_t;

/* ── 数据流条目 ─────────────────────────────────────────────────────── */

typedef struct data_stream {
    uint16_t          data_id;
    sub_node_t*       subs_head;
    struct data_stream* next;
} data_stream_t;

/* ── 管理器 ─────────────────────────────────────────────────────────── */

typedef struct cmd_subscription_mgr {
    data_stream_t*    streams_head;
    hw_mutex_t        lock;
} cmd_subscription_mgr_t;

/* ── 内部: 查找或创建 data_stream ────────────────────────────────────── */

/**
 * 内部：在 streams 链表中查找 data_id，找不到返回 NULL。
 * 调用者必须持有锁。
 */
static data_stream_t* _find_stream(cmd_subscription_mgr_t* mgr, uint16_t data_id)
{
    data_stream_t* ds = mgr->streams_head;
    while (ds) {
        if (ds->data_id == data_id) return ds;
        ds = ds->next;
    }
    return NULL;
}

/**
 * 内部：查找或创建 data_stream。返回 data_stream 指针。
 * 调用者必须持有锁。
 */
static data_stream_t* _ensure_stream(cmd_subscription_mgr_t* mgr, uint16_t data_id)
{
    data_stream_t* ds = _find_stream(mgr, data_id);
    if (ds) return ds;

    ds = (data_stream_t*)calloc(1, sizeof(data_stream_t));
    if (!ds) return NULL;

    ds->data_id = data_id;
    ds->next = mgr->streams_head;
    mgr->streams_head = ds;
    return ds;
}

/* ── 公开 API ───────────────────────────────────────────────────────── */

cmd_subscription_mgr_t* cmd_subscription_create(void)
{
    cmd_subscription_mgr_t* mgr = (cmd_subscription_mgr_t*)calloc(1, sizeof(*mgr));
    if (mgr) hw_mutex_init(&mgr->lock);
    return mgr;
}

void cmd_subscription_destroy(cmd_subscription_mgr_t* mgr)
{
    if (!mgr) return;

    hw_mutex_lock(&mgr->lock);
    data_stream_t* ds = mgr->streams_head;
    while (ds) {
        data_stream_t* ds_next = ds->next;
        sub_node_t* sn = ds->subs_head;
        while (sn) {
            sub_node_t* sn_next = sn->next;
            free(sn);
            sn = sn_next;
        }
        free(ds);
        ds = ds_next;
    }
    hw_mutex_unlock(&mgr->lock);
    hw_mutex_destroy(&mgr->lock);
    free(mgr);
}

int cmd_subscription_add(cmd_subscription_mgr_t* mgr, uint16_t data_id,
                         uint32_t interval_ms, cmd_conn_t* conn)
{
    if (!mgr || !conn) return -1;

    hw_mutex_lock(&mgr->lock);

    /* 检查是否已有订阅（同一 conn + data_id） */
    data_stream_t* ds = _find_stream(mgr, data_id);
    if (ds) {
        sub_node_t* sn = ds->subs_head;
        while (sn) {
            if (sn->conn == conn) {
                sn->interval_ms = interval_ms;  /* 更新间隔 */
                hw_mutex_unlock(&mgr->lock);
                LOG_DEBUG("Subscription updated: data_id=0x%04X interval=%ums",
                          data_id, interval_ms);
                return 0;
            }
            sn = sn->next;
        }
    }

    /* 新建订阅 */
    ds = _ensure_stream(mgr, data_id);
    if (!ds) {
        hw_mutex_unlock(&mgr->lock);
        return -1;
    }

    sub_node_t* sn = (sub_node_t*)calloc(1, sizeof(sub_node_t));
    if (!sn) {
        hw_mutex_unlock(&mgr->lock);
        return -1;
    }
    sn->conn = conn;
    sn->interval_ms = interval_ms;
    sn->next = ds->subs_head;
    ds->subs_head = sn;

    hw_mutex_unlock(&mgr->lock);
    LOG_DEBUG("Subscription added: data_id=0x%04X interval=%ums",
              data_id, interval_ms);
    return 0;
}

int cmd_subscription_remove(cmd_subscription_mgr_t* mgr, uint16_t data_id,
                            cmd_conn_t* conn)
{
    if (!mgr || !conn) return -1;

    hw_mutex_lock(&mgr->lock);

    data_stream_t* ds = _find_stream(mgr, data_id);
    if (!ds) { hw_mutex_unlock(&mgr->lock); return -1; }

    sub_node_t* prev = NULL;
    sub_node_t* sn = ds->subs_head;
    while (sn) {
        if (sn->conn == conn) {
            if (prev) prev->next = sn->next;
            else ds->subs_head = sn->next;
            free(sn);
            hw_mutex_unlock(&mgr->lock);
            LOG_DEBUG("Subscription removed: data_id=0x%04X", data_id);
            return 0;
        }
        prev = sn;
        sn = sn->next;
    }

    hw_mutex_unlock(&mgr->lock);
    return -1;
}

int cmd_subscription_push(cmd_subscription_mgr_t* mgr, uint8_t cmd,
                          uint16_t data_id, const uint8_t* value,
                          size_t value_len)
{
    if (!mgr || !value || value_len == 0) return 0;

    hw_mutex_lock(&mgr->lock);

    data_stream_t* ds = _find_stream(mgr, data_id);
    if (!ds || !ds->subs_head) {
        hw_mutex_unlock(&mgr->lock);
        return 0;
    }

    /* 组推送帧: PAYLOAD = [data_id 2B BE | value ...] */
    size_t pld_len = 2 + value_len;
    uint8_t* pld = (uint8_t*)malloc(pld_len);
    if (!pld) { hw_mutex_unlock(&mgr->lock); return 0; }

    uint16_t id_be = htons(data_id);
    memcpy(pld, &id_be, 2);
    memcpy(pld + 2, value, value_len);

    cmd_frame_t frame;
    frame.cmd     = cmd;
    frame.sub     = cmd_frame_sub_rsp(CMD_SUB_SUBSCRIBE);  /* 0x83 */
    frame.len     = (uint16_t)pld_len;
    frame.payload = pld;

    /* 遍历订阅者发送 */
    int count = 0;
    sub_node_t* sn = ds->subs_head;
    while (sn) {
        if (cmd_conn_send(sn->conn, &frame) == 0) count++;
        sn = sn->next;
    }

    free(pld);
    hw_mutex_unlock(&mgr->lock);
    return count;
}

void cmd_subscription_remove_all(cmd_subscription_mgr_t* mgr, cmd_conn_t* conn)
{
    if (!mgr || !conn) return;

    hw_mutex_lock(&mgr->lock);

    data_stream_t* ds = mgr->streams_head;
    while (ds) {
        sub_node_t* prev = NULL;
        sub_node_t* sn = ds->subs_head;
        while (sn) {
            if (sn->conn == conn) {
                sub_node_t* to_free = sn;
                if (prev) prev->next = sn->next;
                else ds->subs_head = sn->next;
                sn = sn->next;
                free(to_free);
            } else {
                prev = sn;
                sn = sn->next;
            }
        }
        ds = ds->next;
    }

    hw_mutex_unlock(&mgr->lock);
}
