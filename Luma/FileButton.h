
#include "xwidgets.h"
#include "xfile-dialog.h"

#ifdef __cplusplus
extern "C" {
#endif

static void round_rec(cairo_t *cr, float x, float y, float width, float height, float r) {
    cairo_arc(cr, x+r, y+r, r, M_PI, 3*M_PI/2);
    cairo_arc(cr, x+width-r, y+r, r, 3*M_PI/2, 0);
    cairo_arc(cr, x+width-r, y+height-r, r, 0, M_PI/2);
    cairo_arc(cr, x+r, y+height-r, r, M_PI/2, M_PI);
    cairo_close_path(cr);
}

static void draw_image_(Widget_t *w, int width_t, int height_t, float offset) {
    int width, height;
    os_get_surface_size(w->image, &width, &height);
    //double half_width = (width/height >=2) ? width*0.5 : width;
    double x = (double)width_t/(double)(width);
    double y = (double)height_t/(double)height;
    double x1 = (double)height/(double)height_t;
    double y1 = (double)(width)/(double)width_t;
    double off_set = offset*x1;
    cairo_scale(w->crb, x,y);
    if((int)w->adj_y->value) {
        round_rec(w->crb,0, 0, width, height, height* 0.22);
        cairo_set_source_rgba(w->crb, 0.3, 0.3, 0.3, 0.4);
        cairo_fill(w->crb);
    }
    cairo_set_source_surface (w->crb, w->image, off_set, off_set);
    cairo_rectangle(w->crb,0, 0, width, height);
    cairo_fill(w->crb);
    cairo_scale(w->crb, x1,y1);
}

static void draw_i_button(void *w_, void*) {
    Widget_t *w = (Widget_t*)w_;
    if (!w) return;
    Metrics_t metrics;
    os_get_window_metrics(w, &metrics);
    int width = metrics.width-5;
    int height = metrics.height-5;
    if (!metrics.visible) return;
    if (w->image) {
        float offset = 0.0;
        if(w->state==1 && ! (int)w->adj_y->value) {
            offset = 1.0;
        } else if(w->state==1) {
            offset = 2.0;
        } else if(w->state==2) {
            offset = 2.0;
        } else if(w->state==3) {
            offset = 1.0;
        }
        
       draw_image_(w, width, height,offset);
   }
}

static void my_fdialog_response(void *w_, void* user_data) {
    Widget_t *w = (Widget_t*)w_;
    FileButton *filebutton = (FileButton *)w->private_struct;
    if(user_data !=NULL) {
        char *tmp = strdup(*(const char**)user_data);
        free(filebutton->last_path);
        filebutton->last_path = NULL;
        filebutton->last_path = strdup(dirname(tmp));
        filebutton->path = filebutton->last_path;
        free(tmp);
    }
    w->func.user_callback(w,user_data);
    filebutton->is_active = false;
    adj_set_value(w->adj,0.0);
}

static void my_fbutton_callback(void *w_, void*) {
    Widget_t *w = (Widget_t*)w_;
    FileButton *filebutton = (FileButton *)w->private_struct;
    if (w->flags & HAS_POINTER && adj_get_value(w->adj)){
        filebutton->is_active = true;
        if (!filebutton->w) {
            filebutton->w = open_file_dialog(w,filebutton->path,filebutton->filter);
            filebutton->w->flags |= HIDE_ON_DELETE;
#ifdef __linux__
            Atom wmStateAbove = XInternAtom(w->app->dpy, "_NET_WM_STATE_ABOVE", 1 );
            Atom wmNetWmState = XInternAtom(w->app->dpy, "_NET_WM_STATE", 1 );
            XChangeProperty(w->app->dpy, filebutton->w->widget, wmNetWmState, XA_ATOM, 32, 
                PropModeReplace, (unsigned char *) &wmStateAbove, 1); 
#elif defined _WIN32
            os_set_transient_for_hint(w, filebutton->w);
#endif
        } else {
            widget_show_all(filebutton->w);
        }
    } else if (w->flags & HAS_POINTER && !adj_get_value(w->adj)){
        if(filebutton->is_active)
            widget_hide(filebutton->w);
    }
}

static void my_fbutton_mem_free(void *w_, void*) {
    Widget_t *w = (Widget_t*)w_;
    FileButton *filebutton = (FileButton *)w->private_struct;
    free(filebutton->last_path);
    filebutton->last_path = NULL;
    free(filebutton);
    filebutton = NULL;
}

Widget_t *add_my_file_button(Widget_t *parent, int x, int y, int width, int height,
                           const char *path, const char *filter) {
    FileButton *filebutton = (FileButton*)malloc(sizeof(FileButton));
    filebutton->path = path;
    filebutton->filter = filter;
    filebutton->last_path = NULL;
    filebutton->w = NULL;
    filebutton->is_active = false;
    Widget_t *fbutton = add_toggle_button(parent, ". . .", x, y, width, height);
    fbutton->private_struct = filebutton;
    fbutton->flags |= HAS_MEM;
    fbutton->scale.gravity = CENTER;
    fbutton->func.mem_free_callback = my_fbutton_mem_free;
    fbutton->func.value_changed_callback = my_fbutton_callback;
    fbutton->func.dialog_callback = my_fdialog_response;
    fbutton->func.expose_callback = draw_i_button;
    return fbutton;
}


Widget_t* add_lv2_file_button(Widget_t *p, const char * label,
                                    int x, int y, int width, int height) {
    Widget_t* w = add_my_file_button(p, x, y, width, height, "", "");
    w->label = label;
    widget_get_png(w, LDVAR(menu_png));
    return w;
}


#ifdef __cplusplus
}
#endif
