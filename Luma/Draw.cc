
/*
 * Draw.ccc
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2024 brummer <brummer@web.de>
 */

/****************************************************************
        Draw.ccc - lixputty drawings for Luma LV2Host
                   
****************************************************************/

#include "xwidgets.h"


#ifdef __cplusplus
extern "C" {
#endif

static void roundrec(cairo_t *cr, float x, float y, float width, float height, float r) {
    cairo_arc(cr, x+r, y+r, r, M_PI, 3*M_PI/2);
    cairo_arc(cr, x+width-r, y+r, r, 3*M_PI/2, 0);
    cairo_arc(cr, x+width-r, y+height-r, r, 0, M_PI/2);
    cairo_arc(cr, x+r, y+height-r, r, M_PI/2, M_PI);
    cairo_close_path(cr);
}

// draw the window
static void draw_window(void *w_, void* ) {
    Widget_t *w = (Widget_t*)w_;
    if (!w) return;
    set_pattern(w,&w->color_scheme->selected,&w->color_scheme->normal,BACKGROUND_);
    cairo_paint (w->crb);
    set_pattern(w,&w->color_scheme->normal,&w->color_scheme->selected,BACKGROUND_);
    roundrec (w->crb,4,4,w->width-8,w->height-8, 8.0);
    cairo_set_line_width(w->crb,4);
    cairo_stroke(w->crb);
    cairo_new_path (w->crb);
}

static void setButtonFrame(Widget_t* w, Color_state st, int height, int x = 2) {
    Colors *c = get_color_scheme(w,st);
    if (!c) return;
    cairo_pattern_t *pat = cairo_pattern_create_linear (x, x, x, height);
    cairo_pattern_add_color_stop_rgba(pat, 0.0, c->light[0],  c->light[1], c->light[2],  0.4);
    cairo_pattern_add_color_stop_rgba(pat, 0.25, 0.0, 0.0, 0.0, 0.0);
    cairo_pattern_add_color_stop_rgba(pat, 0.75, 0.0, 0.0, 0.0, 0.0);
    cairo_pattern_add_color_stop_rgba(pat, 1.0, c->light[0],  c->light[1], c->light[2],  0.4);
    cairo_set_source(w->crb, pat);
    cairo_pattern_destroy (pat);
}

static void draw_button(void *w_, void* ) {
    Widget_t *w = (Widget_t*)w_;
    if (!w) return;
    Metrics_t metrics;
    os_get_window_metrics(w, &metrics);
    if (!metrics.visible) return;
    int width = metrics.width;
    int height = metrics.height;
    const int state = (int)adj_get_value(w->adj);

    float offset = 0.0;
    if (state) offset = 1.0 ;

    roundrec(w->crb,2.0, 2.0, width-4, height-4, 8.0);

    cairo_pattern_t *pat = cairo_pattern_create_linear( 0, 0, 0, height);
    cairo_pattern_add_color_stop_rgb(pat, 0.00, 0.38, 0.38, 0.38);
    cairo_pattern_add_color_stop_rgb(pat, 0.1, 0.25, 0.25, 0.25);
    cairo_pattern_add_color_stop_rgb(pat, 0.25, 0.14, 0.14, 0.14);
    cairo_pattern_add_color_stop_rgb(pat, 0.65, 0.113, 0.113, 0.113);
    cairo_pattern_add_color_stop_rgb(pat, 1.00, 0.083, 0.083, 0.083);
    cairo_set_source(w->crb, pat);
    cairo_fill(w->crb);
    cairo_pattern_destroy(pat);

    roundrec(w->crb,2.0, 2.0, width-4, height-4, 8.0);
    cairo_set_source_rgba(w->crb, 0.033, 0.033, 0.033, 1);
    cairo_stroke (w->crb);

    cairo_new_path (w->crb);

    cairo_set_line_width(w->crb,  2);
    roundrec(w->crb,3.0, 4.0, width-6, height-8, 8.0);
    cairo_set_source_rgba(w->crb, 0.12, 0.135, 0.135, 1.0);
    cairo_fill (w->crb);
    cairo_new_path (w->crb);       

    
    setButtonFrame(w, PRELIGHT_, height);
    if(w->state==1) use_base_color_scheme(w, INSENSITIVE_);
    cairo_set_line_width(w->crb,  2);
    roundrec(w->crb,1.0, 1.0, width-1, height-1, 8.0);
    cairo_stroke(w->crb);

    if(w->state==1) { // hover
        offset -= 0.5;
    } else if(w->state==2 && !state) { // pressed
        offset += 0.5;
    }

    cairo_new_path (w->crb);

    if (state) {
        roundrec(w->crb,3.0, 4.0, width-6, height-8, 8.0);
        setButtonFrame(w, PRELIGHT_, height, 3);
        cairo_fill_preserve(w->crb);
        cairo_set_source_rgba(w->crb, 0.043, 0.043, 0.143, 0.2);
        cairo_fill_preserve(w->crb);
        cairo_set_source_rgba(w->crb, 0.083, 0.083, 0.083, 1);
        cairo_stroke (w->crb);
        cairo_set_operator (w->crb, CAIRO_OPERATOR_COLOR_DODGE);
    }
    cairo_text_extents_t extents;
    cairo_text_extents(w->crb,"AXy" , &extents);
    int hi = extents.height;
    use_text_color_scheme(w, get_color_state(w));
    cairo_set_font_size (w->crb, (w->app->normal_font+2)/w->scale.ascale);
    cairo_text_extents(w->crb,w->label , &extents);
    cairo_move_to (w->crb, (width-extents.width)*0.5 +offset, (height+hi)*0.5 +offset);
    cairo_show_text(w->crb, w->label);
}

#ifdef __cplusplus
}
#endif
