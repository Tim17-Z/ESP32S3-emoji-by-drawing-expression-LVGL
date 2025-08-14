#include "eyes_blink.h"
#include "lvgl.h"
#include <stdint.h>
#include <stdbool.h>

/* ================== 可调参数 ================== */
#define FRAME_W        120      /* 帧宽 *///备注：1.85c套件设置帧为140*140直接资源耗死了
#define FRAME_H        120      /* 帧高 *///      但帧宽必须得大于（眼半径+间距），否则眼睛就会被竖切变得不圆
#define EYE_R          22      /* 眼睛半径(像素) */
#define EYE_GAP        20      /* 两眼中心间距(像素) */

/* 一次眨眼的总时长（开→闭→开），建议 250~350ms 更自然 */
#define BLINK_MS       300

/* 两次眨眼之间的随机停顿（静默时间） */
#define GAP_MIN_MS     1200
#define GAP_MAX_MS     4000

/* 偶发双连眨的概率（百分比 0~100）以及两次之间的小停顿 */
#define DOUBLE_BLINK_PROB_PCT     18
#define DOUBLE_INNER_GAP_MIN_MS   80
#define DOUBLE_INNER_GAP_MAX_MS   160

/* 眼位轻微抖动参数：最大像素、抖动定时器周期、改变目标的概率 */
#define JITTER_MAX_PX             2
#define JITTER_TICK_MS            120
#define JITTER_CHANGE_PROB_PCT    35   /* 每个 tick 有多大概率换一个新目标偏移 */

/* ================== 颜色(黑底白眼) ================== */
#define COL_BG         lv_color_black()
#define COL_EYE        lv_color_white()

/* ================== 帧缓冲（TRUE_COLOR） ================== */
static lv_color_t s_frame_open [FRAME_W * FRAME_H];
static lv_color_t s_frame_half  [FRAME_W * FRAME_H];
static lv_color_t s_frame_close [FRAME_W * FRAME_H];
static lv_color_t s_frame_half2 [FRAME_W * FRAME_H];

static lv_img_dsc_t s_img_open  = {
    .header.always_zero = 0,
    .header.w = FRAME_W, .header.h = FRAME_H,
    .data_size = sizeof(s_frame_open),
    .header.cf = LV_IMG_CF_TRUE_COLOR,
    .data = (const uint8_t*)s_frame_open
};
static lv_img_dsc_t s_img_half  = {
    .header.always_zero = 0,
    .header.w = FRAME_W, .header.h = FRAME_H,
    .data_size = sizeof(s_frame_half),
    .header.cf = LV_IMG_CF_TRUE_COLOR,
    .data = (const uint8_t*)s_frame_half
};
static lv_img_dsc_t s_img_close = {
    .header.always_zero = 0,
    .header.w = FRAME_W, .header.h = FRAME_H,
    .data_size = sizeof(s_frame_close),
    .header.cf = LV_IMG_CF_TRUE_COLOR,
    .data = (const uint8_t*)s_frame_close
};
static lv_img_dsc_t s_img_half2 = {
    .header.always_zero = 0,
    .header.w = FRAME_W, .header.h = FRAME_H,
    .data_size = sizeof(s_frame_half2),
    .header.cf = LV_IMG_CF_TRUE_COLOR,
    .data = (const uint8_t*)s_frame_half2
};

/* ====== 控件 & 定时器 ====== */
static lv_obj_t   *s_anim = NULL;
static lv_timer_t *s_blink_timer  = NULL;
static lv_timer_t *s_jitter_timer = NULL;

/* ====== 双连眨状态 ====== */
static uint8_t    s_double_phase = 0;  /* 0=不处于双连眨流程；1=等第二次眨 */

/* ====== 抖动状态（当前/目标偏移） ====== */
static int s_offx = 0, s_offy = 0;
static int s_target_offx = 0, s_target_offy = 0;

/* ---------- 简易随机（xorshift），第一次用 lv_tick_get() 作为种子 ---------- */
static uint32_t rnd32(void)
{
    static uint32_t s = 0;
    if (s == 0) s = (uint32_t)lv_tick_get() | 1U;
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return s;
}
static uint32_t rnd_between(uint32_t lo, uint32_t hi)
{
    if (hi <= lo) return lo;
    return lo + (rnd32() % (hi - lo + 1));
}

/* ================ 画帧相关 ================ */
static void fill_bg(lv_color_t *buf)
{
    for (uint32_t i = 0; i < FRAME_W * FRAME_H; i++) buf[i] = COL_BG;
}

/* 画一只“开合度”为 open_pct(0~100) 的眼睛：开=100，闭=0 */
static void draw_one_eye(lv_color_t *buf, int cx, int cy, int r, int open_pct)
{
    int r2 = r * r;
    for (int dy = -r; dy <= r; dy++) {
        int y = cy + dy;
        if (y < 0 || y >= FRAME_H) continue;

        /* 通过垂直裁剪模拟上下眼睑：open_pct 越小，可见的竖向范围越小 */
        int abs_dy = (dy < 0) ? -dy : dy;
        if (abs_dy * 100 > r * open_pct) continue;

        for (int dx = -r; dx <= r; dx++) {
            int x = cx + dx;
            if (x < 0 || x >= FRAME_W) continue;

            if (dx*dx + dy*dy <= r2) {
                buf[y * FRAME_W + x] = COL_EYE;
            }
        }
    }
}

/* 生成一帧：两只眼分别画上 */
static void gen_frame(lv_color_t *buf, int open_pct)
{
    fill_bg(buf);
    int cx1 = FRAME_W/2 - (EYE_GAP/2 + EYE_R);
    int cx2 = FRAME_W/2 + (EYE_GAP/2 + EYE_R);
    int cy  = FRAME_H/2;

    draw_one_eye(buf, cx1, cy, EYE_R, open_pct);
    draw_one_eye(buf, cx2, cy, EYE_R, open_pct);
}

/* ================ 抖动：对齐到中心并添加微小偏移 ================ */
static void apply_jitter_pos(void)
{
    if (!s_anim) return;
    /* 这里用 align 到中心 + 偏移，更稳妥 */
    lv_obj_align(s_anim, LV_ALIGN_CENTER, s_offx, s_offy);
}

static void jitter_timer_cb(lv_timer_t * t)
{
    LV_UNUSED(t);
    if (!s_anim) return;

    /* 有一定概率换一个“目标偏移”，再缓步走过去（更柔和一些） */
    if ((rnd32() % 100) < JITTER_CHANGE_PROB_PCT) {
        s_target_offx = (int)rnd_between(-JITTER_MAX_PX, JITTER_MAX_PX);
        s_target_offy = (int)rnd_between(-JITTER_MAX_PX, JITTER_MAX_PX);
    }

    if (s_offx < s_target_offx) s_offx++;
    else if (s_offx > s_target_offx) s_offx--;

    if (s_offy < s_target_offy) s_offy++;
    else if (s_offy > s_target_offy) s_offy--;

    apply_jitter_pos();
}

/* ================ 眨眼：定时器回调 ================ */
static void blink_timer_cb(lv_timer_t * t)
{
    LV_UNUSED(t);
    if (!s_anim) return;

    /* 开始播放一次眨眼：开→半→闭→半→开（5 帧，时长 BLINK_MS） */
    lv_animimg_start(s_anim);

    if (s_double_phase == 0) {
        /* 第一次触发，判断是否要双连眨 */
        bool do_double = ((rnd32() % 100) < DOUBLE_BLINK_PROB_PCT);
        if (do_double) {
            s_double_phase = 1;
            uint32_t inner_gap = rnd_between(DOUBLE_INNER_GAP_MIN_MS, DOUBLE_INNER_GAP_MAX_MS);
            lv_timer_set_period(s_blink_timer, BLINK_MS + inner_gap);
            lv_timer_reset(s_blink_timer);
            return;
        }
        /* 单次眨眼：安排下一次随机停顿 */
        uint32_t next = BLINK_MS + rnd_between(GAP_MIN_MS, GAP_MAX_MS);
        lv_timer_set_period(s_blink_timer, next);
        lv_timer_reset(s_blink_timer);
    } else {
        /* 第二次眨眼：恢复为正常随机停顿 */
        s_double_phase = 0;
        uint32_t next = BLINK_MS + rnd_between(GAP_MIN_MS, GAP_MAX_MS);
        lv_timer_set_period(s_blink_timer, next);
        lv_timer_reset(s_blink_timer);
    }
}

/* ================ 公有接口 ================ */
void Eyes_create(lv_obj_t *parent)
{
    /* 页面背景置黑 */
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(parent, COL_BG, 0);

    /* 生成 4 个关键帧 */
    gen_frame(s_frame_open , 100);
    gen_frame(s_frame_half ,  50);
    gen_frame(s_frame_close,   0);
    gen_frame(s_frame_half2,  50);

    /* 加一个“开”到末尾，使停顿时保持睁眼 */
    static const lv_img_dsc_t *srcs[] = {
        &s_img_open, &s_img_half, &s_img_close, &s_img_half2, &s_img_open
    };

    s_anim = lv_animimg_create(parent);
    lv_animimg_set_src(s_anim, (const void **)srcs, 5);
    lv_animimg_set_duration(s_anim, BLINK_MS);
    lv_animimg_set_repeat_count(s_anim, 1);  /* 每次只播一遍，由定时器掌控节奏 */

    /* 初始放中间并应用一次抖动对齐 */
    lv_obj_center(s_anim);
    apply_jitter_pos();

    /* 抖动定时器（一直运行，幅度很小） */
    s_jitter_timer = lv_timer_create(jitter_timer_cb, JITTER_TICK_MS, NULL);

    /* 眨眼定时器：首次随机等待再眨第一下 */
    uint32_t first = rnd_between(GAP_MIN_MS, GAP_MAX_MS);
    s_blink_timer = lv_timer_create(blink_timer_cb, first, NULL);
}

void Eyes_close(void)
{
    if (s_blink_timer)  { lv_timer_del(s_blink_timer);  s_blink_timer  = NULL; }
    if (s_jitter_timer) { lv_timer_del(s_jitter_timer); s_jitter_timer = NULL; }
    if (s_anim)         { lv_obj_del(s_anim);           s_anim         = NULL; }
}
