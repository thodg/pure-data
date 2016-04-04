#include "m_pd.h"
#include "g_canvas.h"
#include "m_imp.h"
#include <string.h>

/* ---------- clone - maintain copies of a patch ----------------- */

static t_class *clone_class, *clone_in_class, *clone_out_class;

typedef struct _copy
{
    t_canvas *c_x;
    int c_on;           /* DSP running */
} t_copy;

typedef struct _in
{
    t_class *i_pd;
    struct _clone *i_owner;
    int i_signal;
    int i_n;
} t_in;

typedef struct _out
{
    t_class *o_pd;
    t_outlet *o_outlet;
    int o_signal;
    int o_n;
} t_out;

typedef struct _clone
{
    t_object x_obj;
    int x_n;            /* number of copies */
    t_copy *x_vec;      /* the copies */
    int x_nin;
    t_in *x_invec;
    int x_nout;
    t_out *x_outvec;
    t_symbol *x_s;      /* name of abstraction */
    int x_argc;         /* creation arguments for abstractions */
    t_atom *x_argv;
} t_clone;

void obj_sendinlet(t_object *x, int n, t_symbol *s, int argc, t_atom *argv);

static void clone_in_list(t_in *x, t_symbol *s, int argc, t_atom *argv)
{
    int n;
    if (argc < 1 || argv[0].a_type != A_FLOAT)
        pd_error(x->i_owner, "clone: no instance number in message");
    else if ((n = argv[0].a_w.w_float) < 0 || n >= x->i_owner->x_n)
        pd_error(x->i_owner, "clone: instance number %d out of range", n);
    else if (argc > 1 && argv[1].a_type == A_SYMBOL)
        obj_sendinlet(&x->i_owner->x_vec[n].c_x->gl_obj, x->i_n,
            argv[1].a_w.w_symbol, argc-2, argv+2);
    else obj_sendinlet(&x->i_owner->x_vec[n].c_x->gl_obj, x->i_n,
            &s_list, argc-1, argv+1);
}

static void clone_out_anything(t_out *x, t_symbol *s, int argc, t_atom *argv)
{
        /* TBW */
    post("out %d", x->o_n);
}

static void clone_free(t_clone *x)
{
    int i;
    for (i = 0; i < x->x_n; i++)
    {
        canvas_closebang(x->x_vec[i].c_x);
        pd_free(&x->x_vec[i].c_x->gl_pd);
    }
    t_freebytes(x->x_vec, x->x_n * sizeof(*x->x_vec));
}

extern t_pd *newest;

static t_canvas *clone_makeone(t_symbol *s, int argc, t_atom *argv)
{
    newest = 0;
    t_canvas *retval;
    typedmess(&pd_objectmaker, s, argc, argv);
    if (newest == 0)
    {
        error("clone: can't create subpatch '%s'",
            s->s_name);
        return (0);
    }
    if (*newest != canvas_class)
    {
        error("clone: can't clone '%s' because it's not an abstraction",
            s->s_name);
        pd_free(newest);
        newest = 0;
        return (0);
    }
    retval = (t_canvas *)newest;
    newest = 0;
    retval->gl_owner = 0;
    retval->gl_isclone = 1;
    return (retval);
}

void clone_setn(t_clone *x, t_floatarg f)
{
    int dspstate = canvas_suspend_dsp();
    int nwas = x->x_n, wantn = f, i;
    if (wantn < 1)
    {
        pd_error(x, "can't resize to zero or negative number; setting to 1");
        wantn = 1;
    }
    if (wantn > nwas)
        for (i = nwas; i < wantn; i++)
    {
        t_canvas *c = clone_makeone(x->x_s, x->x_argc, x->x_argv);
        if (!c)
        {
            pd_error(x, "clone: couldn't create '%s'", x->x_s->s_name);
            goto done;
        }
        x->x_vec = (t_copy *)t_resizebytes(x->x_vec, i * sizeof(t_copy *),
            (i+1) * sizeof(t_copy *));
        x->x_vec[i].c_x = c;
        x->x_vec[i].c_on = 0;
        x->x_n++;
    }
    if (wantn < nwas)
    {
        for (i = wantn; i < nwas; i++)
        {
            canvas_closebang(x->x_vec[i].c_x);
            pd_free(&x->x_vec[i].c_x->gl_pd);
        }
        x->x_vec = (t_copy *)t_resizebytes(x->x_vec, nwas * sizeof(t_copy *),
            wantn * sizeof(t_copy *));
        x->x_n = wantn;
    }
done:
    canvas_resume_dsp(dspstate);
}

static void clone_vis(t_clone *x, t_floatarg fn, t_floatarg vis)
{
    int n = fn;
    if (n < 0)
        n = 0;
    else if (n >= x->x_n)
        n = x->x_n - 1;
    canvas_vis(x->x_vec[n].c_x, (vis != 0));
}

static void clone_click(t_clone *x, t_floatarg xpos, t_floatarg ypos,
    t_floatarg shift, t_floatarg ctrl, t_floatarg alt)
{
    clone_vis(x, 0, 1);
}

static void *clone_new(t_symbol *s, int argc, t_atom *argv)
{
    t_clone *x = (t_clone *)pd_new(clone_class);
    t_canvas *c;
    int wantn, dspstate, i;
    x->x_invec = 0;
    x->x_outvec = 0;
    if (argc == 0)
    {
        x->x_vec = 0;
        return (x);
    }
    dspstate = canvas_suspend_dsp();
    {
        static int warned;
        if (!warned)
            post("warning: 'clone' is experimental - may change incompatbly");
        warned = 1;
    }
    if (argc < 2 || (wantn = atom_getfloatarg(0, argc, argv)) <= 0
        || argv[1].a_type != A_SYMBOL)
    {
        error("usage: clone <number> <name> [arguments]");
        goto fail;
    }
    x->x_s = argv[1].a_w.w_symbol;
    x->x_argc = argc - 2;
    x->x_argv = getbytes(x->x_argc * sizeof(*x->x_argv));
    memcpy(x->x_argv, argv+2, x->x_argc * sizeof(*x->x_argv));
    if (!(c = clone_makeone(x->x_s, x->x_argc, x->x_argv)))
        goto fail;
    x->x_vec = (t_copy *)getbytes(sizeof(*x->x_vec));
    x->x_vec[0].c_x = c;
    x->x_n = 1;
    x->x_nin = obj_ninlets(&x->x_vec[0].c_x->gl_obj);
    x->x_invec = (t_in *)getbytes(x->x_nin * sizeof(*x->x_invec));
    for (i = 0; i < x->x_nin; i++)
    {
        x->x_invec[i].i_pd = clone_in_class;
        x->x_invec[i].i_owner = x;
        x->x_invec[i].i_signal = obj_issignalinlet(&x->x_vec[0].c_x->gl_obj, i);
        x->x_invec[i].i_n = i;
        inlet_new(&x->x_vec[0].c_x->gl_obj, &x->x_invec[i].i_pd,
            (x->x_invec[i].i_signal ? &s_signal : 0), 0);
    }
    clone_setn(x, (t_floatarg)(wantn));
    canvas_resume_dsp(dspstate);
    return (x);
fail:
    freebytes(x, sizeof(t_clone));
    canvas_resume_dsp(dspstate);
    return (0);
}

void clone_setup(void)
{
    clone_class = class_new(gensym("clone"), (t_newmethod)clone_new,
        (t_method)clone_free, sizeof(t_clone), 0, A_GIMME, 0);
    class_addmethod(clone_class, (t_method)clone_click, gensym("click"),
        A_FLOAT, A_FLOAT, A_FLOAT, A_FLOAT, A_FLOAT, 0);
    class_addmethod(clone_class, (t_method)clone_vis, gensym("vis"),
        A_FLOAT, A_FLOAT, 0);

    clone_in_class = class_new(gensym("clone-inlet"), 0, 0,
        sizeof(t_in), CLASS_PD, 0);
    class_addlist(clone_in_class, (t_method)clone_in_list);

    clone_out_class = class_new(gensym("clone-outlet"), 0, 0,
        sizeof(t_in), CLASS_PD, 0);
    class_addanything(clone_out_class, (t_method)clone_out_anything);
}
