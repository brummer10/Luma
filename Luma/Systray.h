
/*
 * Systray.h
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2024 brummer <brummer@web.de>
 */

/****************************************************************
        Systray - create a systray icon & menu
****************************************************************/



#pragma once

#include "xwidgets.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int x1, y1, x, y;
    Widget_t *systray;
    Widget_t *systray_menu;
} Systray_t;

/*---------------------------------------------------------------------
-----------------------------------------------------------------------    
                systray responses
-----------------------------------------------------------------------
----------------------------------------------------------------------*/

void getWindowDecorationSize(Widget_t *w, int *width, int *height) {
    Atom type;
    int format;
    unsigned long  count = 0, remaining;
    unsigned char* data = 0;
    long* extents;
    Atom _NET_FRAME_EXTENTS = XInternAtom(w->app->dpy, "_NET_FRAME_EXTENTS", True);
    XGetWindowProperty(w->app->dpy, w->widget, _NET_FRAME_EXTENTS,
        0, 4, False, AnyPropertyType,&type, &format, &count, &remaining, &data);
    extents = (long*) data;
    *height =  static_cast<int>(extents[2])/2;
    *width =  static_cast<int>(extents[0])/2;
    if (data) XFree(data);
}


static void draw_systray(void *w_, void* ) {
    Widget_t *w = (Widget_t*)w_;
    use_systray_color(w);
    cairo_paint (w->crb);
    if (w->image) {
        widget_set_scale(w);
        cairo_set_source_surface (w->crb, w->image, 0, 0);
        cairo_mask_surface (w->crb, w->image, 0, 0);
        widget_reset_scale(w);
    }
}

static void systray_menu_response(void *w_, void* item_, void* ) {
    Widget_t *w = (Widget_t*)w_;
    Widget_t * wid = (Widget_t*)w->parent_struct;
    
    switch (*(int*)item_) {
        case 0:
            destroy_widget(wid, wid->app);
        break;
        default:
        break;
    }
}

static void systray_released(void *w_, void* button_, void*) {
    Widget_t *w = (Widget_t*)w_;
    Widget_t * wid = (Widget_t*)w->parent_struct;
    Systray_t *systray = (Systray_t*)w->private_struct;
    XButtonEvent *xbutton = (XButtonEvent*)button_;
    if (w->flags & HAS_POINTER) {
        if (xbutton->button == Button1) {
            XWindowAttributes attrs;
            XGetWindowAttributes(w->app->dpy, (Window)wid->widget, &attrs);
            if (attrs.map_state == IsViewable) {
                XGetWindowAttributes(w->app->dpy, wid->widget, &attrs);
                systray->x = attrs.x;
                systray->y = attrs.y;
                Window child;
                XTranslateCoordinates( w->app->dpy, wid->widget, DefaultRootWindow(
                    w->app->dpy), attrs.x, attrs.y, &systray->x1, &systray->y1, &child );
                widget_hide(wid);
                XFlush(wid->app->dpy);
            } else {
                int x = 0;
                int y = 0;
                widget_show_all(wid);
                getWindowDecorationSize(wid, &x, &y);
                os_move_window(w->app->dpy, wid, systray->x1 - systray->x - x, systray->y1 - systray->y - y);
                XFlush(wid->app->dpy);
            }
        } else if (xbutton->button == Button3) {
            pop_menu_show(w,systray->systray_menu,3,true);
        }
    }
}

static void create_systray_menu(Widget_t * wid, Systray_t *systray) {
    systray->systray_menu = create_menu(wid,25);
    systray->systray_menu->private_struct = systray;
    systray->systray_menu->parent_struct = wid;
    menu_add_item(systray->systray_menu,_("Quit"));
    systray->systray_menu->func.button_release_callback = systray_menu_response;
}

static void systray_mem_free(void *w_, void*) {
    Widget_t *w = (Widget_t*)w_;
    Systray_t *systray = (Systray_t*)w->private_struct;
    free(systray);
}

void create_systray_widget(Widget_t * wid, int x, int y, int w, int h) {
    Systray_t *systray = (Systray_t*)malloc(sizeof(Systray_t));
    systray->systray = create_window(wid->app, DefaultRootWindow(wid->app->dpy), x, y, w, h);
    systray->systray->private_struct = systray;
    systray->systray->parent_struct = wid;
    systray->systray->flags |= HAS_MEM;
    systray->systray->func.mem_free_callback = systray_mem_free;
    systray->systray->func.expose_callback = draw_systray;
    widget_get_png(systray->systray, LDVAR(LV2Host_png));
    systray->systray->func.button_release_callback = systray_released;
    send_systray_message(systray->systray);
    create_systray_menu(wid, systray);   
}

#ifdef __cplusplus
}
#endif
