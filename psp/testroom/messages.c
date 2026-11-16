/* The event scripts' on-screen messages (EventTblNN.bin's `message` and `message_wait`, ops 6e and 6f).
 *
 * tools/gen_messages.py keeps every BLOCK of a ROOM section that shows a message, op for op, with the text from
 * the disc's msg_eng.pac (a local generated header, never in the repo). This runs those blocks the way the
 * game's interpreter does (docs/EVENT_SCRIPTS.md): each frame a block that isn't done runs from its start; its
 * switches pick a case, `end_block 0` stops it for this frame, `end_block 1` or its end finishes it (type 0 once
 * a mission, type 1 once a visit). `message` opens the text box, `message_wait` holds the block until it's
 * closed, `wait n` holds it n frames.
 *
 * What it can answer: the room's areas (EVE quads), examine (O pressed and not taken by something else), items
 * got (item_state 1), enemies cleared, the room and mission. Flags and counters are its own, set only by these
 * blocks' own set_flag / counter ops: those the rest of the port keeps in C (a seal lit, a trial done) aren't
 * seen here, so a message gated on one of those reads them as clear. Everything else in a block (cameras,
 * sounds, gimmicks, items given or taken) is skipped: the missions' own code does those. */
#include <stdio.h>
#include <string.h>

#include "messages.h"
#include "messages_runtime.h"

#define FLAG_MAX 256
#define COUNTER_MAX 32
static unsigned char flags[FLAG_MAX], event_flags[FLAG_MAX];
static int counters[COUNTER_MAX];
static unsigned char done[MSG_BLOCK_COUNT];
static short pcs[MSG_BLOCK_COUNT];
static float waits[MSG_BLOCK_COUNT];
static int mission, room, running = -1;
static const char *text;          /* the open message, or 0 */
static int page, shown_frames, shown_id;
static int shown_total;

static const MsgArea *area_of(int r, int a) {
    int i;
    for (i = 0; i < MSG_AREA_COUNT; ++i) if (msg_areas[i].room == r && msg_areas[i].area == a) return &msg_areas[i];
    return 0;
}
static int inside(int a, float x, float y, float z) {
    const MsgArea *q = area_of(room, a);
    int k, pos = 0, neg = 0;
    if (!q || y < q->y - 100.0f || y > q->y + q->height) return 0;
    for (k = 0; k < 4; ++k) {
        const float *p = q->xz[k], *n = q->xz[(k + 1) & 3];
        float c = (n[0] - p[0]) * (z - p[1]) - (n[1] - p[1]) * (x - p[0]);
        if (c > 0.0f) pos = 1; else if (c < 0.0f) neg = 1;
    }
    return !(pos && neg);
}
static const char *text_of(int id) {
    int i;
    for (i = 0; i < MSG_TEXT_COUNT; ++i) if (msg_texts[i].id == id) return msg_texts[i].text;
    return 0;
}

void msg_reset(void) {
    memset(flags, 0, sizeof flags); memset(event_flags, 0, sizeof event_flags);
    memset(counters, 0, sizeof counters); memset(done, 0, sizeof done);
    text = 0; running = -1; shown_total = 0;
}

void msg_enter(int m, int r) {
    int i;
    mission = m; room = r; running = -1; text = 0;
    for (i = 0; i < MSG_BLOCK_COUNT; ++i) {
        pcs[i] = 0; waits[i] = 0.0f;
        if (msg_blocks[i].kind == 1) done[i] = 0;   /* once a visit */
    }
}

static int switch_value(const MsgOp *o, const MsgEnv *env) {
    switch (o->op) {
    case 0x0f: return inside(o->a, env->x, env->y, env->z);
    case 0x10: return o->a >= 0 && o->a < FLAG_MAX ? flags[o->a] : 0;
    case 0x11: return o->a >= 0 && o->a < COUNTER_MAX ? counters[o->a] : 0;
    case 0x12: return o->a >= 0 && o->a < FLAG_MAX ? event_flags[o->a] : 0;
    case 0x14: case 0x15: return env->item_got ? env->item_got(o->a) : 0;
    case 0x17: case 0x18: return env->enemies_left == 0;
    case 0x19: return env->enemies_left;
    case 0x1c: return env->examine;
    case 0x1f: return room;
    case 0x20: return mission;
    default: return 0;
    }
}
static int is_switch(int op) { return op >= 0x0f && op <= 0x2b; }
static int is_case(int op) { return op >= 0x2c && op <= 0x30; }
/* From just after a switch (or inside one of its cases), the index of its end_switch. */
static int end_of_switch(const MsgOp *ops, int at, int count) {
    int depth = 0;
    for (; at < count; ++at) {
        if (is_switch(ops[at].op)) ++depth;
        else if (ops[at].op == 0x31) { if (depth-- == 0) return at; }
    }
    return count;
}
static int pick_case(const MsgOp *ops, int at, int count, int value) {
    int depth = 0;
    for (; at < count; ++at) {
        int op = ops[at].op, v = ops[at].a;
        if (is_switch(op)) ++depth;
        else if (op == 0x31) { if (depth-- == 0) return at + 1; }
        else if (depth == 0 && is_case(op) &&
                 (op == 0x30 || (op == 0x2c && value == v) || (op == 0x2d && value != v) || (op == 0x2e && value >= v) || (op == 0x2f && value <= v)))
            return at + 1;
    }
    return count;
}

/* Run block b from its pc until it stops, waits or ends. Returns 1 while it's holding (a message or a wait). */
static int run(int b, const MsgEnv *env) {
    const MsgBlock *blk = &msg_blocks[b];
    const MsgOp *ops = &msg_ops[blk->first];
    int pc = pcs[b], steps = 0;
    while (pc < blk->count && steps++ < 512) {
        const MsgOp *o = &ops[pc];
        if (is_switch(o->op)) { pc = pick_case(ops, pc + 1, blk->count, switch_value(o, env)); continue; }
        if (is_case(o->op)) { pc = end_of_switch(ops, pc + 1, blk->count) + 1; continue; }   /* the end of the case taken */
        switch (o->op) {
        case 0x3d:   /* end_block: 1 finishes it, 0 stops it for now */
            pcs[b] = 0;
            if (o->a) done[b] = 1;
            return 0;
        case 0x6e: {
            const char *t = text_of(o->a);
            if (t) { text = t; page = 0; shown_frames = 0; shown_id = o->a; ++shown_total; }
            ++pc; continue;
        }
        case 0x6f:
            if (text) { pcs[b] = (short)pc; return 1; }
            ++pc; continue;
        case 0x58: case 0x8c:
            if (waits[b] <= 0.0f && o->a > 0) { waits[b] = (float)o->a / 60.0f; pcs[b] = (short)pc; return 1; }
            if (waits[b] > 0.0f) { pcs[b] = (short)pc; return 1; }
            ++pc; continue;
        case 0x65: if (o->a >= 0 && o->a < FLAG_MAX) flags[o->a] = (unsigned char)(o->b != 0); break;
        case 0x66: if (o->a >= 0 && o->a < COUNTER_MAX) counters[o->a] = o->b; break;
        case 0x67: if (o->a >= 0 && o->a < COUNTER_MAX) ++counters[o->a]; break;
        case 0x68: if (o->a >= 0 && o->a < COUNTER_MAX) --counters[o->a]; break;
        case 0x69: if (o->a >= 0 && o->a < FLAG_MAX) event_flags[o->a] = (unsigned char)(o->b != 0); break;
        default: break;
        }
        ++pc;
    }
    pcs[b] = 0;
    done[b] = 1;   /* ran to its end */
    return 0;
}

int msg_update(float dt, unsigned int pressed, const MsgEnv *env) {
    int i;
    if (text) {
        /* The box: O or X turns the page; the last one closes it. Not on the frame it opened. */
        if (++shown_frames > 1 && (pressed & (MSG_BUTTON_NEXT))) {
            const char *p = text;
            int k;
            for (k = 0; k <= page && p; ++k) p = strchr(p, '\f') ? strchr(p, '\f') + 1 : 0;
            if (p) ++page; else text = 0;
        }
        if (text) return 1;
    }
    if (running >= 0) {
        if (waits[running] > 0.0f && (waits[running] -= dt) <= 0.0f) { waits[running] = 0.0f; ++pcs[running]; }
        if (waits[running] > 0.0f) return 0;
        if (!run(running, env)) running = -1;
        return text != 0;
    }
    for (i = 0; i < MSG_BLOCK_COUNT; ++i) {
        if (done[i] || msg_blocks[i].mission != mission || msg_blocks[i].room != room) continue;
        if (run(i, env)) { running = i; break; }
        if (text) break;
    }
    return text != 0;
}

int msg_busy(void) { return text != 0; }

int msg_page(char lines[MSG_LINES][MSG_LINE_CHARS]) {
    const char *p = text;
    int k, n = 0;
    if (!p) return 0;
    for (k = 0; k < page && p; ++k) p = strchr(p, '\f') ? strchr(p, '\f') + 1 : 0;
    if (!p) return 0;
    while (*p && *p != '\f' && n < MSG_LINES) {
        int c = 0;
        while (*p && *p != '\f' && *p != '\n') {
            if (c < MSG_LINE_CHARS - 1) lines[n][c++] = *p;
            ++p;
        }
        lines[n++][c] = 0;
        if (*p == '\n') ++p;
    }
    return n;
}

void msg_status(char *out, int size) {
    int i, left = 0;
    for (i = 0; i < MSG_BLOCK_COUNT; ++i) left += msg_blocks[i].mission == mission && msg_blocks[i].room == room && !done[i];
    snprintf(out, size, "msg mission=%d room=%d blocks_left=%d open=%d id=%d page=%d shown=%d", mission, room, left, text != 0, shown_id, page, shown_total);
}
