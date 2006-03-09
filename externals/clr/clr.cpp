// mono
extern "C" {
#include <mono/jit/jit.h>
#include <mono/metadata/object.h>
#include <mono/metadata/environment.h>
#include <mono/metadata/assembly.h>
#include <mono/metadata/debug-helpers.h>
#include <mono/metadata/class.h>
#include <mono/metadata/metadata.h>
}

#ifdef _MSC_VER
#pragma warning(disable: 4091)
#endif
#include <m_pd.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h> // for _close
#define close _close
#else
#include <unistd.h>
#endif

#include <map>
#include <vector>
#include <list>

#define CORELIB "PureData"
#define DLLEXT "dll"


// cached mono data

static MonoDomain *monodomain;
static MonoClass *clr_symbol,*clr_pointer,*clr_atom,*clr_atomlist,*clr_external;
static MonoMethodDesc *clr_desc_tostring,*clr_desc_ctor;
static MonoMethod *clr_meth_invoke;
static MonoProperty *clr_prop_method;

struct AtomList
{
    int argc;
    t_atom *argv;

    void Set(int c,t_atom *v) { argc = c,argv = v; }
};


// temporary workspace items

static MonoArray *clr_objarr_1,*clr_objarr_3;
static MonoObject *clr_obj_int,*clr_obj_single,*clr_obj_symbol,*clr_obj_pointer,*clr_obj_atomlist;
static int *clr_val_int;
static float *clr_val_single;
static t_symbol **clr_val_symbol;
static void **clr_val_pointer;
static AtomList *clr_val_atomlist;


struct t_clr;

struct Delegate
{
    enum Kind { k_bang,k_float,k_symbol,k_pointer,k_list,k_anything };

    inline operator bool() const { return methodinfo != NULL; }

    MonoObject *methodinfo;
    MonoMethod *virtmethod;
    Kind kind;

    inline void init(MonoObject *method,Kind k) 
    {
        methodinfo = mono_property_get_value(clr_prop_method,method,NULL,NULL);
        virtmethod = mono_object_get_virtual_method(methodinfo,clr_meth_invoke);
        kind = k;
    }

    inline MonoObject *operator()(MonoObject *obj,void *arg = NULL) const
    {
        gpointer args[2] = {obj,arg};
        assert(methodinfo);
        MonoObject *exc;
        mono_runtime_invoke(virtmethod,methodinfo,args,&exc);
        return exc;
    }

    inline MonoObject *invokeatom(MonoObject *obj,MonoObject *atom) const
    {
        mono_array_set(clr_objarr_1,void*,0,atom);
        return operator()(obj,clr_objarr_1);
    }

    inline MonoObject *operator()(MonoObject *obj,float f) const
    {
        *clr_val_single = f;
        return invokeatom(obj,clr_obj_single);
    }

    inline MonoObject *operator()(MonoObject *obj,t_symbol *s) const
    {
        *clr_val_symbol = s;
        return invokeatom(obj,clr_obj_symbol);
    }

    inline MonoObject *operator()(MonoObject *obj,t_gpointer *p) const
    {
        *clr_val_pointer = p;
        return invokeatom(obj,clr_obj_pointer);
    }

    inline MonoObject *operator()(MonoObject *obj,int argc,t_atom *argv) const
    {
        clr_val_atomlist->Set(argc,argv);
        return invokeatom(obj,clr_obj_atomlist);
    }

    inline MonoObject *operator()(MonoObject *obj,int inlet,t_symbol *s,int argc,t_atom *argv) const
    {
        *clr_val_int = inlet;
        *clr_val_symbol = s;
        clr_val_atomlist->Set(argc,argv);
        mono_array_set(clr_objarr_3,void*,0,clr_obj_int);
        mono_array_set(clr_objarr_3,void*,1,clr_obj_symbol);
        mono_array_set(clr_objarr_3,void*,2,clr_obj_atomlist);
        return operator()(obj,clr_objarr_3);
    }
};

typedef std::map<t_symbol *,Delegate> ClrMethodMap;
typedef std::vector<ClrMethodMap *> ClrMethods;

// this is the class structure
// holding the pd and mono class
// and our CLR method pointers
struct t_clr_class
{
    t_class *pd_class;
    MonoClass *mono_class;
    MonoMethod *mono_ctor;
    MonoClassField *obj_field; // ptr field in PureData.External
    t_symbol *name;
    Delegate method_bang,method_float,method_symbol,method_pointer,method_list,method_anything;
    ClrMethods *methods; // explicit method selectors
};

static t_class *proxy_class;

struct t_proxy
{
    t_object pd_obj; // myself
    t_clr *parent; // parent object
    int inlet;
};

typedef std::map<t_symbol *,t_clr_class *> ClrMap;
// this holds the class name to class structure association
static ClrMap clr_map;

typedef std::vector<t_outlet *> OutletArr;
typedef std::list<t_proxy *> ProxyList;

// this is the class to be setup (while we are in the CLR static Main method)
static t_clr_class *clr_setup_class = NULL;

// inlet index... must start with 0 every time a new object is made
static int clr_inlet;

// this is the class instance object structure
struct t_clr
{
    t_object pd_obj; // myself
    t_clr_class *clr_clss; // pointer to our class structure
    MonoObject *mono_obj;  // the mono class instance
    OutletArr *outlets;
    ProxyList *proxies;
};



// Print error message given by exception
static void error_exc(char *txt,char *cname,MonoObject *exc)
{
    MonoMethod *m = mono_method_desc_search_in_class(clr_desc_tostring,mono_get_exception_class());
    assert(m);
    m = mono_object_get_virtual_method(exc,m);
    assert(m);
    MonoString *str = (MonoString *)mono_runtime_invoke(m,exc,NULL,NULL);
    assert(str);
    error("CLR class %s: %s",txt,cname);
    error(mono_string_to_utf8(str));
}

static void clr_method_bang(t_clr *x) 
{
    assert(x && x->clr_clss);
    MonoObject *exc = x->clr_clss->method_bang(x->mono_obj);
    if(exc) error_exc("Exception raised",x->clr_clss->name->s_name,exc);
}

static void clr_method_float(t_clr *x,t_float f) 
{
    assert(x && x->clr_clss);
    MonoObject *exc = x->clr_clss->method_float(x->mono_obj,f);
    if(exc) error_exc("Exception raised",x->clr_clss->name->s_name,exc);
}

static void clr_method_symbol(t_clr *x,t_symbol *s) 
{
    assert(x && x->clr_clss);
    MonoObject *exc = x->clr_clss->method_symbol(x->mono_obj,s);
    if(exc) error_exc("Exception raised",x->clr_clss->name->s_name,exc);
}

static void clr_method_pointer(t_clr *x,t_gpointer *p)
{
    assert(x && x->clr_clss);
    MonoObject *exc = x->clr_clss->method_pointer(x->mono_obj,p);
    if(exc) error_exc("Exception raised",x->clr_clss->name->s_name,exc);
}

static void clr_method_list(t_clr *x,t_symbol *,int argc,t_atom *argv)
{
    assert(x && x->clr_clss);
    MonoObject *exc = x->clr_clss->method_symbol(x->mono_obj,argc,argv);
    if(exc) error_exc("Exception raised",x->clr_clss->name->s_name,exc);
}

static void call_anything(t_clr *x,int inlet,t_symbol *s,int argc,t_atom *argv)
{
    assert(x && x->clr_clss);

    const Delegate *d;

    ClrMethods *methods = x->clr_clss->methods;
    if(methods && inlet < (int)methods->size()) {
        ClrMethodMap *methmap = (*methods)[inlet];
        if(methmap) {
            ClrMethodMap::iterator it = methmap->find(s);
            if(it != methmap->end()) {
                d = &it->second;
                goto found;
            }

            if(inlet) {
                // search for NULL symbol pointer
                it = methmap->find(NULL);
                if(it != methmap->end()) {
                    d = &it->second;
                    goto found;
                }
            }
        }
    }

    // no selectors: general method
    d = &x->clr_clss->method_anything;

    found:

    // we must have found something....
    assert(d);

    MonoObject *exc;
    switch(d->kind) {
        case Delegate::k_bang: 
            assert(argc == 0);
            exc = (*d)(x->mono_obj); 
            break;
        case Delegate::k_float: 
            assert(argc == 1 && argv[0].a_type == A_FLOAT);
            exc = (*d)(x->mono_obj,argv[0].a_w.w_float); 
            break;
        case Delegate::k_symbol:
            assert(argc == 1 && argv[0].a_type == A_SYMBOL);
            exc = (*d)(x->mono_obj,argv[0].a_w.w_symbol); 
            break;
        case Delegate::k_pointer:
            assert(argc == 1 && argv[0].a_type == A_POINTER);
            exc = (*d)(x->mono_obj,argv[0].a_w.w_gpointer); 
            break;
        case Delegate::k_list:
            exc = (*d)(x->mono_obj,argc,argv); 
            break;
        case Delegate::k_anything: 
            exc = (*d)(x->mono_obj,inlet,s,argc,argv);  
            break;
        default:
            assert(false);
    }

    if(exc) error_exc("Exception raised",x->clr_clss->name->s_name,exc);
}

static void clr_method_anything(t_clr *x,t_symbol *s,int argc,t_atom *argv) { call_anything(x,0,s,argc,argv); }
static void clr_method_proxy(t_proxy *x,t_symbol *s,int argc,t_atom *argv) { call_anything(x->parent,x->inlet,s,argc,argv); }

static void PD_Post(MonoString *str)
{
	post("%s",mono_string_to_utf8(str));	
}

static void PD_PostError(MonoString *str)
{
	error("%s",mono_string_to_utf8(str));	
}

static void PD_PostVerbose(int lvl,MonoString *str)
{
	verbose(lvl,"%s",mono_string_to_utf8(str));	
}

static void *PD_SymGen(MonoString *str)
{
    assert(str);
	return gensym(mono_string_to_utf8(str));	
}

static MonoString *PD_SymEval(t_symbol *sym)
{
    assert(sym);
    return mono_string_new(monodomain,sym->s_name);
}

static void PD_AddMethodHandler(int inlet,t_symbol *sym,MonoObject *method,Delegate::Kind kind)
{
    assert(clr_setup_class);
    Delegate d;
    d.methodinfo = mono_property_get_value(clr_prop_method,method,NULL,NULL);
    d.virtmethod = mono_object_get_virtual_method(d.methodinfo,clr_meth_invoke);
    d.kind = kind;

    ClrMethods *ms = clr_setup_class->methods;
    if(!ms)
        clr_setup_class->methods = ms = new ClrMethods;

    ClrMethodMap *m;
    if(inlet >= (int)ms->size()) {
        ms->resize(inlet+1,NULL);
        (*ms)[inlet] = m = new ClrMethodMap;
    }
    else
        m = (*ms)[inlet];
    assert(m);
        
    // add tag to map
    (*m)[sym] = d;
}

static void PD_AddMethodSelector(int inlet,t_symbol *sym,MonoObject *method)
{
    PD_AddMethodHandler(inlet,sym,method,Delegate::k_anything);
}

static void PD_AddMethodBang(int inlet,MonoObject *method)
{
    if(inlet)
        PD_AddMethodHandler(inlet,&s_bang,method,Delegate::k_bang);
    else {
        assert(clr_setup_class);
        clr_setup_class->method_bang.init(method,Delegate::k_bang);
    }
}

static void PD_AddMethodFloat(int inlet,MonoObject *method)
{
    if(inlet)
        PD_AddMethodHandler(inlet,&s_float,method,Delegate::k_float);
    else {
        assert(clr_setup_class);
        clr_setup_class->method_float.init(method,Delegate::k_float);
    }
}

static void PD_AddMethodSymbol(int inlet,MonoObject *method)
{
    if(inlet)
        PD_AddMethodHandler(inlet,&s_symbol,method,Delegate::k_symbol);
    else {
        assert(clr_setup_class);
        clr_setup_class->method_symbol.init(method,Delegate::k_symbol);
    }
}

static void PD_AddMethodPointer(int inlet,MonoObject *method)
{
    if(inlet)
        PD_AddMethodHandler(inlet,&s_pointer,method,Delegate::k_pointer);
    else {
        assert(clr_setup_class);
        clr_setup_class->method_pointer.init(method,Delegate::k_pointer);
    }
}

static void PD_AddMethodList(int inlet,MonoObject *method)
{
    if(inlet)
        PD_AddMethodHandler(inlet,&s_list,method,Delegate::k_list);
    else {
        assert(clr_setup_class);
        clr_setup_class->method_list.init(method,Delegate::k_list);
    }
}

static void PD_AddMethodAnything(int inlet,MonoObject *method)
{
    if(inlet)
        PD_AddMethodHandler(inlet,NULL,method,Delegate::k_anything);
    else {
        assert(clr_setup_class);
        clr_setup_class->method_anything.init(method,Delegate::k_anything);
    }
}


static void PD_AddInletAlias(t_clr *obj,t_symbol *sel,t_symbol *to_sel)
{
    ++clr_inlet;
    assert(obj);
    t_inlet *in = inlet_new(&obj->pd_obj,&obj->pd_obj.ob_pd,sel,to_sel);
    assert(in);
}

static void PD_AddInletFloat(t_clr *obj,float *f)
{
    ++clr_inlet;
    assert(obj);
    t_inlet *in = floatinlet_new(&obj->pd_obj,f);
    assert(in);
}

static void PD_AddInletSymbol(t_clr *obj,t_symbol **s)
{
    ++clr_inlet;
    assert(obj);
    t_inlet *in = symbolinlet_new(&obj->pd_obj,s);
    assert(in);
}

/*
static void PD_AddInletPointer(t_clr *obj,t_gpointer *p)
{
    ++clr_inlet;
    assert(obj);
    t_inlet *in = pointerinlet_new(&obj->pd_obj,p);
    assert(in);
}
*/

static void PD_AddInletProxyTyped(t_clr *obj,t_symbol *type)
{
    assert(obj);
    t_proxy *p = (t_proxy *)pd_new(proxy_class);
    p->parent = obj;
    p->inlet = ++clr_inlet;
    if(!obj->proxies) obj->proxies = new ProxyList;
    obj->proxies->push_back(p);
    t_inlet *in = inlet_new(&obj->pd_obj,&p->pd_obj.ob_pd,type,type);
    assert(in);
}

static void PD_AddInletProxy(t_clr *obj) { PD_AddInletProxyTyped(obj,NULL); }


static void PD_AddOutlet(t_clr *obj,t_symbol *type)
{
    assert(obj);
    t_outlet *out = outlet_new(&obj->pd_obj,type);
    assert(out);
    if(!obj->outlets) obj->outlets = new OutletArr;
    obj->outlets->push_back(out);
}

static void PD_OutletBang(t_clr *obj,int n)
{
    assert(obj);
    assert(obj->outlets);
    assert(n >= 0 && n < (int)obj->outlets->size());
    outlet_bang((*obj->outlets)[n]);
}

static void PD_OutletFloat(t_clr *obj,int n,float f)
{
    assert(obj);
    assert(obj->outlets);
    assert(n >= 0 && n < (int)obj->outlets->size());
    outlet_float((*obj->outlets)[n],f);
}

static void PD_OutletSymbol(t_clr *obj,int n,t_symbol *s)
{
    assert(obj);
    assert(obj->outlets);
    assert(n >= 0 && n < (int)obj->outlets->size());
    outlet_symbol((*obj->outlets)[n],s);
}

static void PD_OutletPointer(t_clr *obj,int n,t_gpointer *p)
{
    assert(obj);
    assert(obj->outlets);
    assert(n >= 0 && n < (int)obj->outlets->size());
    outlet_pointer((*obj->outlets)[n],p);
}

static void PD_OutletAtom(t_clr *obj,int n,t_atom l)
{
    assert(obj);
    assert(obj->outlets);
    assert(n >= 0 && n < (int)obj->outlets->size());
    t_outlet *out = (*obj->outlets)[n];
    switch(l.a_type) {
        case A_FLOAT: outlet_float(out,l.a_w.w_float); break;
        case A_SYMBOL: outlet_symbol(out,l.a_w.w_symbol); break;
        case A_POINTER: outlet_pointer(out,l.a_w.w_gpointer); break;
        default:
            error("CLR - internal error in file " __FILE__ ", line %i",__LINE__);
    }
}

static void PD_OutletAnything(t_clr *obj,int n,t_symbol *s,AtomList l)
{
    assert(obj);
    assert(obj->outlets);
    assert(n >= 0 && n < (int)obj->outlets->size());
    outlet_anything((*obj->outlets)[n],s,l.argc,l.argv);
}

static void PD_OutletAnything2(t_clr *obj,int n,t_symbol *s,MonoArray *l)
{
    assert(obj);
    assert(obj->outlets);
    assert(n >= 0 && n < (int)obj->outlets->size());
//    assert(mono_object_get_class(&l->obj) == clr_atom);
    outlet_anything((*obj->outlets)[n],s,mono_array_length(l),mono_array_addr(l,t_atom,0));
}

static void PD_SendAtom(t_symbol *dst,t_atom a)
{
    void *cl = dst->s_thing;
    if(cl) pd_forwardmess((t_class **)cl,1,&a);
}

static void PD_SendAnything(t_symbol *dst,t_symbol *s,AtomList l)
{
    void *cl = dst->s_thing;
    if(cl) pd_typedmess((t_class **)cl,s,l.argc,l.argv);
}

static void PD_SendAnything2(t_symbol *dst,t_symbol *s,MonoArray *l)
{
    void *cl = dst->s_thing;
//    assert(mono_object_get_class(&l->obj) == clr_atom);
    if(cl) pd_typedmess((t_class **)cl,s,mono_array_length(l),mono_array_addr(l,t_atom,0));
}

void *clr_new(t_symbol *classname, int argc, t_atom *argv)
{
    // find class name in map

    ClrMap::iterator it = clr_map.find(classname);
    if(it == clr_map.end()) {
        error("CLR class %s not found",classname->s_name);
        return NULL;
    }

    t_clr_class *clss = it->second;

    // make instance
    t_clr *x = (t_clr *)pd_new(clss->pd_class);
    x->mono_obj = mono_object_new (monodomain,clss->mono_class);
    if(!x->mono_obj) {
        pd_free((t_pd *)x);
        error("CLR class %s could not be instantiated",classname->s_name);
        return NULL;
    }

    // store class pointer
    x->clr_clss = clss;
    x->clr_clss->name = classname;

    // store our object pointer in External::ptr member
    mono_field_set_value(x->mono_obj,clss->obj_field,x);

    x->outlets = NULL;
    x->proxies = NULL;

    // ok, we have an object - look for constructor
	if(clss->mono_ctor) {
        // reset inlet index
        clr_inlet = 0;

        AtomList al; 
        al.Set(argc,argv);
	    gpointer args = &al;

        // call constructor
        MonoObject *exc;
        mono_runtime_invoke(clss->mono_ctor,x->mono_obj,&args,&exc);

        if(exc) {
            pd_free((t_pd *)x);
            error_exc("exception raised in constructor",classname->s_name,exc);
            return NULL;
        }
    }
    else
        verbose(1,"CLR - Warning: no constructor for class %s found",classname->s_name);

    return x;
}

void clr_free(t_clr *obj)
{
    if(obj->outlets) delete obj->outlets;

    if(obj->proxies) {
        for(ProxyList::iterator it = obj->proxies->begin(); it != obj->proxies->end(); ++it) pd_free((t_pd *)*it);
        delete obj->proxies;
    }
}

static int classloader(char *dirname, char *classname)
{
    t_clr_class *clr_class = NULL;
    t_symbol *classsym;
    MonoAssembly *assembly;
    MonoImage *image;
    MonoMethod *method;

    char dirbuf[MAXPDSTRING],*nameptr;
    // search for classname.dll in the PD path
    int fd;
    if ((fd = open_via_path(dirname, classname, "." DLLEXT, dirbuf, &nameptr, MAXPDSTRING, 1)) < 0)
        // not found
        goto bailout;

    // found
    close(fd);

    clr_class = (t_clr_class *)getbytes(sizeof(t_clr_class));
    // set all struct members to 0
    memset(clr_class,0,sizeof(*clr_class));

    // try to load assembly
    strcat(dirbuf,"/");
    strcat(dirbuf,classname);
    strcat(dirbuf,"." DLLEXT);

    assembly = mono_domain_assembly_open(monodomain,dirbuf);
	if(!assembly) {
		error("clr: file %s couldn't be loaded!",dirbuf);
		goto bailout;
	}

    image = mono_assembly_get_image(assembly);
    assert(image);

    // try to find class
    // "" means no namespace
	clr_class->mono_class = mono_class_from_name(image,"",classname);
	if(!clr_class->mono_class) {
		error("Can't find %s class in %s\n",classname,mono_image_get_filename(image));
		goto bailout;
	}

    // make new class (with classname)
    classsym = gensym(classname);
    clr_class->pd_class = NULL;
    int flags = CLASS_DEFAULT;

    clr_class->obj_field = mono_class_get_field_from_name(clr_class->mono_class,"ptr");
    assert(clr_class->obj_field);

    // find static Main method

    MonoMethodDesc *clr_desc_main = mono_method_desc_new("::Setup",FALSE);
    assert(clr_desc_main);

    method = mono_method_desc_search_in_class(clr_desc_main,clr_class->mono_class);
	if(method) {
        MonoObject *obj = mono_object_new(monodomain,clr_class->mono_class);
        if(!obj) {
            error("CLR class %s could not be instantiated",classname);
            goto bailout;
        }

        // store NULL in External::ptr member
        mono_field_set_value(obj,clr_class->obj_field,NULL);

        // set current class
        clr_setup_class = clr_class;

        // call static Main method
	    gpointer args = obj;
        MonoObject *exc;
        MonoObject *ret = mono_runtime_invoke(method,NULL,&args,&exc);
        if(ret) {
            // \TODO check return type
            flags = *(int *)mono_object_unbox(ret);
        }

        // unset current class
        clr_setup_class = NULL;

        if(exc) {
            error_exc("CLR - Exception raised by Setup",classname,exc);
            goto bailout;
        }
    }
    else
        post("CLR - Warning: no %s.Setup method found",classname);

    // find and save constructor
    clr_class->mono_ctor = mono_method_desc_search_in_class(clr_desc_ctor,clr_class->mono_class);

    // make pd class
    clr_class->pd_class = class_new(classsym,(t_newmethod)clr_new,(t_method)clr_free, sizeof(t_clr), flags, A_GIMME, A_NULL);

    // register methods
    if(clr_class->method_bang)
        class_addbang(clr_class->pd_class,clr_method_bang);
    if(clr_class->method_float)
        class_addfloat(clr_class->pd_class,clr_method_float);
    if(clr_class->method_symbol)
        class_addsymbol(clr_class->pd_class,clr_method_symbol);
    if(clr_class->method_pointer)
        class_addpointer(clr_class->pd_class,clr_method_pointer);
    if(clr_class->method_list)
        class_addlist(clr_class->pd_class,clr_method_list);
    if(clr_class->method_anything || clr_class->methods)
        class_addanything(clr_class->pd_class,clr_method_anything);

    // put into map
    clr_map[classsym] = clr_class;

    verbose(1,"CLR - Loaded class %s OK",classname);

    return 1;

bailout:
    if(clr_class) freebytes(clr_class,sizeof(t_clr_class));

    return 0;
}

extern "C"
#ifdef _MSC_VER
__declspec(dllexport) 
#endif
void clr_setup(void)
{
#ifdef _WIN32
    // set mono paths
    const char *monopath = getenv("MONO_PATH");
    if(!monopath) {
        error("CLR - Please set the MONO_PATH environment variable to the folder of your MONO installation - CLR not loaded!");
        return;
    }
    
    char tlib[256],tconf[256];
    strcpy(tlib,monopath);
    strcat(tlib,"/lib");
    strcpy(tconf,monopath);
    strcat(tconf,"/etc");
    mono_set_dirs(tlib,tconf);
#endif

    try { 
        monodomain = mono_jit_init(CORELIB); 
    }
    catch(...) {
        monodomain = NULL;
    }

	if(monodomain) {
        // try to find PureData.dll in the PD path
        char dirbuf[MAXPDSTRING],*nameptr;
        // search for classname.dll in the PD path
        int fd;
        if ((fd = open_via_path("",CORELIB,"." DLLEXT,dirbuf,&nameptr,MAXPDSTRING,1)) >= 0) {
            strcat(dirbuf,"/" CORELIB "." DLLEXT);
            close(fd);
        }
        else 
            strcpy(dirbuf,CORELIB "." DLLEXT);

        // look for PureData.dll
        MonoAssembly *assembly = mono_domain_assembly_open (monodomain,dirbuf);
	    if(!assembly) {
		    error("clr: assembly " CORELIB "." DLLEXT " not found!");
		    return;
	    }

	    MonoImage *image = mono_assembly_get_image(assembly);
        assert(image);

	    // add mono to C hooks

        mono_add_internal_call("PureData.Internal::SymGen(string)", (const void *)PD_SymGen);
        mono_add_internal_call("PureData.Internal::SymEval(void*)", (const void *)PD_SymEval);

        mono_add_internal_call("PureData.External::Post(string)",(const void *)PD_Post);
        mono_add_internal_call("PureData.External::PostError(string)",(const void *)PD_PostError);
        mono_add_internal_call("PureData.External::PostVerbose(int,string)",(const void *)PD_PostVerbose);

        mono_add_internal_call("PureData.External::AddMethod(int,PureData.External/MethodBang)", (const void *)PD_AddMethodBang);
        mono_add_internal_call("PureData.External::AddMethod(int,PureData.External/MethodFloat)", (const void *)PD_AddMethodFloat);
        mono_add_internal_call("PureData.External::AddMethod(int,PureData.External/MethodSymbol)", (const void *)PD_AddMethodSymbol);
        mono_add_internal_call("PureData.External::AddMethod(int,PureData.External/MethodPointer)", (const void *)PD_AddMethodPointer);
        mono_add_internal_call("PureData.External::AddMethod(int,PureData.External/MethodList)", (const void *)PD_AddMethodList);
        mono_add_internal_call("PureData.External::AddMethod(int,PureData.Symbol,PureData.External/MethodAnything)", (const void *)PD_AddMethodSelector);
        mono_add_internal_call("PureData.External::AddMethod(int,PureData.External/MethodAnything)", (const void *)PD_AddMethodAnything);

        mono_add_internal_call("PureData.Internal::AddInlet(void*,PureData.Symbol,PureData.Symbol)", (const void *)PD_AddInletAlias);
        mono_add_internal_call("PureData.Internal::AddInlet(void*,single&)", (const void *)PD_AddInletFloat);
        mono_add_internal_call("PureData.Internal::AddInlet(void*,PureData.Symbol&)", (const void *)PD_AddInletSymbol);
//        mono_add_internal_call("PureData.Internal::AddInlet(void*,PureData.Pointer&)", (const void *)PD_AddInletPointer);
//        mono_add_internal_call("PureData.Internal::AddInlet(void*,PureData.Symbol)", (const void *)PD_AddInletTyped);
        mono_add_internal_call("PureData.Internal::AddInlet(void*)", (const void *)PD_AddInletProxy);

        mono_add_internal_call("PureData.Internal::AddOutlet(void*,PureData.Symbol)", (const void *)PD_AddOutlet);

        mono_add_internal_call("PureData.Internal::Outlet(void*,int)", (const void *)PD_OutletBang);
        mono_add_internal_call("PureData.Internal::Outlet(void*,int,single)", (const void *)PD_OutletFloat);
        mono_add_internal_call("PureData.Internal::Outlet(void*,int,PureData.Symbol)", (const void *)PD_OutletSymbol);
        mono_add_internal_call("PureData.Internal::Outlet(void*,int,PureData.Pointer)", (const void *)PD_OutletPointer);
        mono_add_internal_call("PureData.Internal::Outlet(void*,int,PureData.Atom)", (const void *)PD_OutletAtom);
        mono_add_internal_call("PureData.Internal::Outlet(void*,int,PureData.Symbol,PureData.AtomList)", (const void *)PD_OutletAnything);
        mono_add_internal_call("PureData.Internal::Outlet(void*,int,PureData.Symbol,PureData.Atom[])", (const void *)PD_OutletAnything2);

//        mono_add_internal_call("PureData.Internal::Bind(void*,PureData.Symbol)", (const void *)PD_Bind);
//        mono_add_internal_call("PureData.Internal::Unbind(void*,PureData.Symbol)", (const void *)PD_Unbind);

        mono_add_internal_call("PureData.External::Send(PureData.Symbol,PureData.Atom)", (const void *)PD_SendAtom);
        mono_add_internal_call("PureData.External::Send(PureData.Symbol,PureData.Symbol,PureData.AtomList)", (const void *)PD_SendAnything);
        mono_add_internal_call("PureData.External::Send(PureData.Symbol,PureData.Symbol,PureData.Atom[])", (const void *)PD_SendAnything2);

        // load important classes
        clr_symbol = mono_class_from_name(image,"PureData","Symbol");
        assert(clr_symbol);
        clr_pointer = mono_class_from_name(image,"PureData","Pointer");
        assert(clr_pointer);
        clr_atom = mono_class_from_name(image,"PureData","Atom");
        assert(clr_atom);
        clr_atomlist = mono_class_from_name(image,"PureData","AtomList");
        assert(clr_atomlist);
        clr_external = mono_class_from_name(image,"PureData","External");
        assert(clr_external);

        clr_desc_tostring = mono_method_desc_new("::ToString()",FALSE);
        assert(clr_desc_tostring);
        clr_desc_ctor = mono_method_desc_new("::.ctor(AtomList)",FALSE);
        assert(clr_desc_ctor);

        MonoMethodDesc *desc = mono_method_desc_new("System.Reflection.MethodBase:Invoke(object,object[])",FALSE);
        clr_meth_invoke = mono_method_desc_search_in_image(desc,mono_get_corlib());

        MonoClass *delegate = mono_class_from_name(mono_get_corlib(),"System","Delegate");
        assert(delegate);
        clr_prop_method = mono_class_get_property_from_name(delegate,"Method");
        assert(clr_prop_method);

        // static objects to avoid allocation at method call time
        clr_objarr_1 = mono_array_new(monodomain,mono_get_object_class(),1);
        clr_objarr_3 = mono_array_new(monodomain,mono_get_object_class(),3);
        clr_obj_int = mono_object_new(monodomain,mono_get_int32_class());
        clr_obj_single = mono_object_new(monodomain,mono_get_single_class());
        clr_obj_symbol = mono_object_new(monodomain,clr_symbol);
        clr_obj_pointer = mono_object_new(monodomain,clr_pointer);
        clr_obj_atomlist = mono_object_new(monodomain,clr_atomlist);
        // unboxed addresses
        clr_val_int = (int *)mono_object_unbox(clr_obj_int);
        clr_val_single = (float *)mono_object_unbox(clr_obj_single);
        clr_val_symbol = (t_symbol **)mono_object_unbox(clr_obj_symbol);
        clr_val_pointer = (void **)mono_object_unbox(clr_obj_pointer);
        clr_val_atomlist = (AtomList *)mono_object_unbox(clr_obj_atomlist);

        // make proxy class
        proxy_class = class_new(gensym("clr proxy"),NULL,NULL,sizeof(t_proxy),CLASS_PD|CLASS_NOINLET,A_NULL);
        class_addanything(proxy_class,clr_method_proxy);

        // install loader hook
        sys_loader(classloader);

        // ready!
	    post("CLR extension - (c)2006 Davide Morelli, Thomas Grill");
    }
    else
        error("clr: mono domain couldn't be initialized!");
}
