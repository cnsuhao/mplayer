
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <inttypes.h>

#include "config.h"
#include "mp_msg.h"
#include "x11_common.h"

#ifdef X11_FULLSCREEN

#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#include "video_out.h"

#include <X11/Xmd.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

#ifdef HAVE_XDPMS
#include <X11/extensions/dpms.h>
#endif

#ifdef HAVE_XINERAMA
#include <X11/extensions/Xinerama.h>
#endif

#ifdef HAVE_XF86VM
#include <X11/extensions/xf86vmode.h>
#endif

#include "../input/input.h"
#include "../input/mouse.h"

#ifdef HAVE_NEW_GUI
#include "../Gui/interface.h"
#include "../mplayer.h"
#endif

#define vo_wm_Unknown     0
#define vo_wm_NetWM       1
#define vo_wm_Layered     2

#define WIN_LAYER_ONBOTTOM               2
#define WIN_LAYER_NORMAL                 4
#define WIN_LAYER_ONTOP                  6
#define WIN_LAYER_ABOVE_DOCK             10
 
int ice_layer=WIN_LAYER_ABOVE_DOCK;
int stop_xscreensaver=0;

static int dpms_disabled=0;
static int timeout_save=0;
static int xscreensaver_was_running=0;
static int kdescreensaver_was_running=0;

char* mDisplayName=NULL;
Display* mDisplay=NULL;
Window   mRootWin;
int mScreen;
int mLocalDisplay;

/* output window id */
int WinID=-1;
int vo_mouse_autohide = 0;
int vo_wm_type = -1;

#define SUPPORT_NONE 0
#define SUPPORT_FULLSCREEN 1
#define SUPPORT_ABOVE 2
#define SUPPORT_STAYS_ON_TOP 4
int net_wm_support = 0;

static int vo_old_x = 0;
static int vo_old_y = 0;
static int vo_old_width = 0;
static int vo_old_height = 0;


#ifdef HAVE_XINERAMA
int xinerama_screen = 0;
int xinerama_x = 0;
int xinerama_y = 0;
#endif
#ifdef HAVE_XF86VM
XF86VidModeModeInfo **vidmodes=NULL;
XF86VidModeModeLine modeline;
#endif

void vo_hidecursor ( Display *disp , Window win )
{
	Cursor no_ptr;
	Pixmap bm_no;
	XColor black,dummy;
	Colormap colormap;
	static unsigned char bm_no_data[] = { 0,0,0,0, 0,0,0,0  };

	if(WinID==0) return;	// do not hide, if we're playing at rootwin
	
	colormap = DefaultColormap(disp,DefaultScreen(disp));
	XAllocNamedColor(disp,colormap,"black",&black,&dummy);	
	bm_no = XCreateBitmapFromData(disp, win, bm_no_data, 8,8);    
	no_ptr=XCreatePixmapCursor(disp, bm_no, bm_no,&black, &black,0, 0);									          
	XDefineCursor(disp,win,no_ptr);
	XFreeCursor( disp,no_ptr );
}

void vo_showcursor( Display *disp, Window win )
{ 
 if ( WinID==0 ) return;
 XDefineCursor( disp,win,0 ); 
}

static int x11_errorhandler(Display *display, XErrorEvent *event)
{
#define MSGLEN 60
    char msg[MSGLEN];
        
    XGetErrorText(display, event->error_code, (char *)&msg, MSGLEN);
    
    mp_msg(MSGT_VO,MSGL_ERR,"X11 error: %s\n", msg);
    
    mp_msg(MSGT_VO,MSGL_V,"Type: %x, display: %x, resourceid: %x, serial: %x\n",
	    event->type, event->display, event->resourceid, event->serial);
    mp_msg(MSGT_VO,MSGL_V,"Error code: %x, request code: %x, minor code: %x\n",
	    event->error_code, event->request_code, event->minor_code);
    
    abort();
    //exit_player("X11 error");
#undef MSGLEN
}

int net_wm_support_state_test( char *name )
{
  mp_dbg(MSGT_VO,MSGL_DBG2, "[x11] NetWM supports %s\n",name);
 if ( !strncmp( name,"_NET_WM_STATE_FULLSCREEN", 24 ) )
  { mp_dbg( MSGT_VO,MSGL_STATUS,"[x11] Detected wm supports FULLSCREEN state.\n" ); return SUPPORT_FULLSCREEN; }
 if ( !strncmp( name,"_NET_WM_STATE_STAYS_ON_TOP", 26 ) )
  { mp_dbg( MSGT_VO,MSGL_STATUS,"[x11] Detected wm supports STAYS_ON_TOP state.\n" ); return SUPPORT_STAYS_ON_TOP; }
 if ( !strncmp( name,"_NET_WM_STATE_ABOVE", 19 ) )
  { mp_dbg( MSGT_VO,MSGL_STATUS,"[x11] Detected wm supports ABOVE state.\n" ); return SUPPORT_ABOVE; }
 return SUPPORT_NONE;
}

int vo_wm_detect( void )
{
 Atom            type;
 XEvent          xev;
 int             i;
 int             wm = vo_wm_Unknown;
 int             format;
 unsigned long   nitems, bytesafter;
 Atom          * args = NULL;
 
 if ( WinID >= 0 ) return vo_wm_Unknown;
 
// -- supports layers
 type=XInternAtom( mDisplay,"_WIN_PROTOCOLS",False );
 if ( Success == XGetWindowProperty( mDisplay,mRootWin,type,0,16384,False,AnyPropertyType,&type,&format,&nitems,&bytesafter,(unsigned char **) &args ) && nitems > 0 )
  {
   mp_dbg( MSGT_VO,MSGL_STATUS,"[x11] Detected wm supports layers.\n" );
   for (i = 0; i < nitems; i++)
     if (!strcmp( XGetAtomName (mDisplay, args[i]), "_WIN_LAYER"))
     {
       XFree( args );
       return vo_wm_Layered;
     }
   XFree( args );
  }

// --- netwm 
 type=XInternAtom( mDisplay,"_NET_SUPPORTED",False );
 if ( Success == XGetWindowProperty( mDisplay,mRootWin,type,0,16384,False,AnyPropertyType,&type,&format,&nitems,&bytesafter,(unsigned char **) &args ) && nitems > 0 )
  {
   mp_dbg( MSGT_VO,MSGL_STATUS,"[x11] Detected wm is of class NetWM.\n" );
   for (i = 0; i < nitems; i++)
     net_wm_support |= net_wm_support_state_test (XGetAtomName (mDisplay, args[i]));
   XFree( args );
   if (net_wm_support)
   {
     // ugly hack for broken OpenBox _NET_WM_STATE_FULLSCREEN support
     // (in their implementation it only changes internal state of window, nothing more!!!)
     if (vo_wm_NetWM == SUPPORT_FULLSCREEN)
     {
        type=XInternAtom( mDisplay,"_BLACKBOX_PID",False );
	if ( Success == XGetWindowProperty( mDisplay,mRootWin,type,0,16384,False,AnyPropertyType,&type,&format,&nitems,&bytesafter,(unsigned char **) &args ) && nitems > 0 )
	{
           mp_dbg( MSGT_VO,MSGL_STATUS,"[x11] Detected wm is a broken OpenBox.\n" );
	   net_wm_support=0;
	   XFree( args );
           return vo_wm_Unknown;
	}
	XFree (args);
     }
     return vo_wm_NetWM;
   }
  }

 if ( wm == vo_wm_Unknown ) mp_dbg( MSGT_VO,MSGL_STATUS,"[x11] Unknown wm type...\n" );
 return wm;
}    

int vo_init( void )
{
// int       mScreen;
 int depth, bpp;
 unsigned int mask;
// char    * DisplayName = ":0.0";
// Display * mDisplay;
 XImage  * mXImage = NULL;
// Window    mRootWin;
 XWindowAttributes attribs;
 char* dispName;

 if(vo_depthonscreen) return 1; // already called

 XSetErrorHandler(x11_errorhandler);

#if 0
 if (!mDisplayName)
   if (!(mDisplayName=getenv("DISPLAY")))
     mDisplayName=strdup(":0.0");
#else
  dispName = XDisplayName(mDisplayName);
#endif

 mp_msg(MSGT_VO,MSGL_V,"X11 opening display: %s\n", dispName);

 mDisplay=XOpenDisplay(dispName);
 if ( !mDisplay )
  {
   mp_msg(MSGT_VO,MSGL_ERR,"vo: couldn't open the X11 display (%s)!\n",dispName );
   return 0;
  }
 mScreen=DefaultScreen( mDisplay );     // Screen ID.
 mRootWin=RootWindow( mDisplay,mScreen );// Root window ID.

#ifdef HAVE_XINERAMA
 if(XineramaIsActive(mDisplay))
  {
  XineramaScreenInfo *screens;
  int num_screens;

  screens = XineramaQueryScreens(mDisplay, &num_screens);
  if(xinerama_screen >= num_screens) xinerama_screen = 0;
  if (! vo_screenwidth)
    vo_screenwidth=screens[xinerama_screen].width;
  if (! vo_screenheight)
    vo_screenheight=screens[xinerama_screen].height;
  xinerama_x = screens[xinerama_screen].x_org;
  xinerama_y = screens[xinerama_screen].y_org;

  XFree(screens);
  }
 else
#endif
#ifdef HAVE_XF86VM
 {
  int clock;
  XF86VidModeGetModeLine( mDisplay,mScreen,&clock ,&modeline );
  if ( !vo_screenwidth )  vo_screenwidth=modeline.hdisplay;
  if ( !vo_screenheight ) vo_screenheight=modeline.vdisplay;
 }
#endif
 {
 if (! vo_screenwidth)
   vo_screenwidth=DisplayWidth( mDisplay,mScreen );
 if (! vo_screenheight)
   vo_screenheight=DisplayHeight( mDisplay,mScreen );
 }
 // get color depth (from root window, or the best visual):
 XGetWindowAttributes(mDisplay, mRootWin, &attribs);
 depth=attribs.depth;

 if (depth != 15 && depth != 16 && depth != 24 && depth != 32) {
   Visual *visual;

   depth = vo_find_depth_from_visuals(mDisplay, mScreen, &visual);
   if (depth != -1)
     mXImage=XCreateImage(mDisplay, visual, depth, ZPixmap,
			  0, NULL, 1, 1, 8, 1);
 } else
   mXImage=XGetImage( mDisplay,mRootWin,0,0,1,1,AllPlanes,ZPixmap );

 vo_depthonscreen = depth;	// display depth on screen

 // get bits/pixel from XImage structure:
 if (mXImage == NULL) {
   mask = 0;
 } else {
   /*
    * for the depth==24 case, the XImage structures might use
    * 24 or 32 bits of data per pixel.  The global variable
    * vo_depthonscreen stores the amount of data per pixel in the
    * XImage structure!
    *
    * Maybe we should rename vo_depthonscreen to (or add) vo_bpp?
    */
   bpp=mXImage->bits_per_pixel;
   if((vo_depthonscreen+7)/8 != (bpp+7)/8) vo_depthonscreen=bpp; // by A'rpi
   mask=mXImage->red_mask|mXImage->green_mask|mXImage->blue_mask;
   mp_msg(MSGT_VO,MSGL_V,"vo: X11 color mask:  %X  (R:%lX G:%lX B:%lX)\n",
	    mask,mXImage->red_mask,mXImage->green_mask,mXImage->blue_mask);
   XDestroyImage( mXImage );
 }
 if(((vo_depthonscreen+7)/8)==2){
   if(mask==0x7FFF) vo_depthonscreen=15; else
   if(mask==0xFFFF) vo_depthonscreen=16;
 }
// XCloseDisplay( mDisplay );
/* slightly improved local display detection AST */
 if ( strncmp(dispName, "unix:", 5) == 0)
		dispName += 4;
 else if ( strncmp(dispName, "localhost:", 10) == 0)
		dispName += 9;
 if (*dispName==':' && atoi(dispName+1)<10) mLocalDisplay=1; else mLocalDisplay=0;
 mp_msg(MSGT_VO,MSGL_INFO,"vo: X11 running at %dx%d with depth %d and %d bpp (\"%s\" => %s display)\n",
	vo_screenwidth,vo_screenheight,
	depth, vo_depthonscreen,
	dispName,mLocalDisplay?"local":"remote");

 vo_wm_type=vo_wm_detect();

 saver_off(mDisplay);
 return 1;
}

void vo_uninit( void )
{
 if (!mDisplay)
 {
    mp_msg(MSGT_VO, MSGL_V, "vo: x11 uninit called but X11 not inited..\n");
    return;
 }
// if( !vo_depthonscreen ) return;
 mp_msg(MSGT_VO,MSGL_V,"vo: uninit ...\n" );
 XSetErrorHandler(NULL);
 XCloseDisplay( mDisplay );
 vo_depthonscreen = 0;
 mDisplay=NULL;
}

#include "../linux/keycodes.h"
#include "wskeys.h"

extern void mplayer_put_key(int code);

void vo_x11_putkey(int key){
 switch ( key )
  {
   case wsLeft:      mplayer_put_key(KEY_LEFT); break;
   case wsRight:     mplayer_put_key(KEY_RIGHT); break;
   case wsUp:        mplayer_put_key(KEY_UP); break;
   case wsDown:      mplayer_put_key(KEY_DOWN); break;
   case wsSpace:     mplayer_put_key(' '); break;
   case wsEscape:    mplayer_put_key(KEY_ESC); break;
   case wsEnter:     mplayer_put_key(KEY_ENTER); break;
   case wsBackSpace: mplayer_put_key(KEY_BS); break;
   case wsDelete:    mplayer_put_key(KEY_DELETE); break;
   case wsInsert:    mplayer_put_key(KEY_INSERT); break;
   case wsHome:      mplayer_put_key(KEY_HOME); break;
   case wsEnd:       mplayer_put_key(KEY_END); break;
   case wsPageUp:    mplayer_put_key(KEY_PAGE_UP); break;
   case wsPageDown:  mplayer_put_key(KEY_PAGE_DOWN); break;
   case wsq:
   case wsQ:         mplayer_put_key('q'); break;
   case wsp:
   case wsP:         mplayer_put_key('p'); break;
   case wsMinus:
   case wsGrayMinus: mplayer_put_key('-'); break;
   case wsPlus:
   case wsGrayPlus:  mplayer_put_key('+'); break;
   case wsGrayMul:
   case wsMul:       mplayer_put_key('*'); break;
   case wsGrayDiv:
   case wsDiv:       mplayer_put_key('/'); break;
   case wsLess:      mplayer_put_key('<'); break;
   case wsMore:      mplayer_put_key('>'); break;
   case wsm:
   case wsM:	     mplayer_put_key('m'); break;
   case wso:
   case wsO:         mplayer_put_key('o'); break;
   default: if((key>='a' && key<='z')||(key>='A' && key<='Z')||
	       (key>='0' && key<='9')) mplayer_put_key(key);
  }

}


// ----- Motif header: -------

#define MWM_HINTS_FUNCTIONS     (1L << 0)
#define MWM_HINTS_DECORATIONS   (1L << 1)
#define MWM_HINTS_INPUT_MODE    (1L << 2)
#define MWM_HINTS_STATUS        (1L << 3)

#define MWM_FUNC_ALL            (1L << 0)
#define MWM_FUNC_RESIZE         (1L << 1)
#define MWM_FUNC_MOVE           (1L << 2)
#define MWM_FUNC_MINIMIZE       (1L << 3)
#define MWM_FUNC_MAXIMIZE       (1L << 4)
#define MWM_FUNC_CLOSE          (1L << 5)

#define MWM_DECOR_ALL           (1L << 0)
#define MWM_DECOR_BORDER        (1L << 1)
#define MWM_DECOR_RESIZEH       (1L << 2)
#define MWM_DECOR_TITLE         (1L << 3)
#define MWM_DECOR_MENU          (1L << 4)
#define MWM_DECOR_MINIMIZE      (1L << 5)
#define MWM_DECOR_MAXIMIZE      (1L << 6)

#define MWM_INPUT_MODELESS 0
#define MWM_INPUT_PRIMARY_APPLICATION_MODAL 1
#define MWM_INPUT_SYSTEM_MODAL 2
#define MWM_INPUT_FULL_APPLICATION_MODAL 3
#define MWM_INPUT_APPLICATION_MODAL MWM_INPUT_PRIMARY_APPLICATION_MODAL

#define MWM_TEAROFF_WINDOW      (1L<<0)

typedef struct
{
  long flags;
  long functions;
  long decorations;
  long input_mode;
  long state;
} MotifWmHints;

extern MotifWmHints vo_MotifWmHints;
extern Atom         vo_MotifHints;
extern int          vo_depthonscreen;
extern int          vo_screenwidth;
extern int          vo_screenheight;

static MotifWmHints   vo_MotifWmHints;
static Atom           vo_MotifHints  = None;

// Note: always d==0 !
void vo_x11_decoration( Display * vo_Display,Window w,int d )
{

  if ( !WinID ) return;

  if(vo_fsmode&1){
    XSetWindowAttributes attr;
    attr.override_redirect = (!d) ? True : False;
    XChangeWindowAttributes(vo_Display, w, CWOverrideRedirect, &attr);
//    XMapWindow(vo_Display, w);
  }

  if(vo_fsmode&8){
    XSetTransientForHint (vo_Display, w, RootWindow(vo_Display,mScreen));
  }

 vo_MotifHints=XInternAtom( vo_Display,"_MOTIF_WM_HINTS",0 );
 if ( vo_MotifHints != None )
  {
   memset( &vo_MotifWmHints,0,sizeof( MotifWmHints ) );
   vo_MotifWmHints.flags=MWM_HINTS_FUNCTIONS | MWM_HINTS_DECORATIONS;
   if ( d )
    {
     vo_MotifWmHints.functions=MWM_FUNC_MOVE | MWM_FUNC_CLOSE | MWM_FUNC_MINIMIZE | MWM_FUNC_MAXIMIZE | MWM_FUNC_RESIZE;
     d=MWM_DECOR_ALL;
    }
#if 0
   vo_MotifWmHints.decorations=d|((vo_fsmode&2)?0:MWM_DECOR_MENU);
#else
   vo_MotifWmHints.decorations=d|((vo_fsmode&2)?MWM_DECOR_MENU:0);
#endif
   XChangeProperty( vo_Display,w,vo_MotifHints,vo_MotifHints,32,
                    PropModeReplace,(unsigned char *)&vo_MotifWmHints,(vo_fsmode&4)?4:5 );
  }
}

void vo_x11_classhint( Display * display,Window window,char *name ){
	    XClassHint wmClass;
	    pid_t pid=getpid();
	    wmClass.res_name = name;
	    wmClass.res_class = "MPlayer";
	    XSetClassHint(display,window,&wmClass);
	    XChangeProperty(display,window, XInternAtom(display,"_NET_WM_PID", False), XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&pid, 1);
}

Window     vo_window = None;
GC         vo_gc = NULL;
XSizeHints vo_hint;

#ifdef HAVE_NEW_GUI
 void vo_setwindow( Window w,GC g ) {
   vo_window=w; vo_gc=g;
 }
#endif

void vo_x11_uninit()
{
    saver_on(mDisplay);
    if(vo_window!=None) vo_showcursor( mDisplay,vo_window );

#ifdef HAVE_NEW_GUI
    /* destroy window only if it's not controlled by GUI */
    if ( !use_gui )
#endif
    {
	if(vo_gc){
	  XSetBackground( mDisplay,vo_gc,0 );
	  XFreeGC( mDisplay,vo_gc );
	  vo_gc=NULL;
	}
	if(vo_window!=None){
	  XClearWindow( mDisplay,vo_window );
	  if (WinID < 0){
	    XEvent xev;
	    XUnmapWindow( mDisplay,vo_window );
	    XDestroyWindow(mDisplay, vo_window);
	    do { XNextEvent( mDisplay,&xev ); } while ( xev.type != DestroyNotify || xev.xdestroywindow.event != vo_window );
	  }
	  vo_window=None;
	}
	vo_fs=0;
	vo_old_width=vo_old_height=0;
    }
}

       int vo_mouse_timer_const = 30;
static int vo_mouse_counter = 30;

int vo_x11_check_events(Display *mydisplay){
 int ret=0;
 XEvent         Event;
 char           buf[100];
 KeySym         keySym;
 static XComposeStatus stat;

// unsigned long  vo_KeyTable[512];

 if ( ( vo_mouse_autohide )&&( --vo_mouse_counter == 0 ) ) vo_hidecursor( mydisplay,vo_window );

 while ( XPending( mydisplay ) )
  {
   XNextEvent( mydisplay,&Event );
   #ifdef HAVE_NEW_GUI
    if ( use_gui ) 
     {
      guiGetEvent( 0,(char*)&Event );
      if ( vo_window != Event.xany.window ) continue;
     }
   #endif
//       printf("\rEvent.type=%X  \n",Event.type);
    switch( Event.type )
     {
      case Expose:
           ret|=VO_EVENT_EXPOSE;
           break;
      case ConfigureNotify:
//	   if (!vo_fs && (Event.xconfigure.width == vo_screenwidth || Event.xconfigure.height == vo_screenheight)) break;
//	   if (vo_fs && Event.xconfigure.width != vo_screenwidth && Event.xconfigure.height != vo_screenheight) break;
           if ( vo_window == None ) break;
           vo_dwidth=Event.xconfigure.width;
           vo_dheight=Event.xconfigure.height;
#if 0
	   /* when resizing, x and y are zero :( */
	   vo_dx=Event.xconfigure.x;
	   vo_dy=Event.xconfigure.y;
#else
	   {
	    Window root;
	    int foo;
	    Window win;
	    XGetGeometry(mydisplay, vo_window, &root, &foo, &foo, 
		&foo/*width*/, &foo/*height*/, &foo, &foo);
	    XTranslateCoordinates(mydisplay, vo_window, root, 0, 0,
		&vo_dx, &vo_dy, &win);
	    }
#endif
           ret|=VO_EVENT_RESIZE;
           break;
      case KeyPress:
           { 
	    int key;
            XLookupString( &Event.xkey,buf,sizeof(buf),&keySym,&stat );
	    key=( (keySym&0xff00) != 0?( (keySym&0x00ff) + 256 ):( keySym ) );
	    #ifdef HAVE_NEW_GUI
	     if ( ( use_gui )&&( key == wsEnter ) ) break;
	    #endif
            vo_x11_putkey( key );
            ret|=VO_EVENT_KEYPRESS;
	   }
           break;
      case MotionNotify:
           if ( vo_mouse_autohide ) { vo_showcursor( mydisplay,vo_window ); vo_mouse_counter=vo_mouse_timer_const; }
           break;
      case ButtonPress:
           if ( vo_mouse_autohide ) { vo_showcursor( mydisplay,vo_window ); vo_mouse_counter=vo_mouse_timer_const; }
           // Ignore mouse whell press event
           if(Event.xbutton.button > 3) {
	   mplayer_put_key(MOUSE_BTN0+Event.xbutton.button-1);
	   break;
           }
	   #ifdef HAVE_NEW_GUI
	    // Ignor mouse button 1 - 3 under gui 
	    if ( use_gui && ( Event.xbutton.button >= 1 )&&( Event.xbutton.button <= 3 ) ) break;
	   #endif
           mplayer_put_key((MOUSE_BTN0+Event.xbutton.button-1)|MP_KEY_DOWN);
           break;
      case ButtonRelease:
           if ( vo_mouse_autohide ) { vo_showcursor( mydisplay,vo_window ); vo_mouse_counter=vo_mouse_timer_const; }
           #ifdef HAVE_NEW_GUI
	    // Ignor mouse button 1 - 3 under gui 
	    if ( use_gui && ( Event.xbutton.button >= 1 )&&( Event.xbutton.button <= 3 ) ) break;
	   #endif
           mplayer_put_key(MOUSE_BTN0+Event.xbutton.button-1);
           break;
      case PropertyNotify: 
    	   {
	    char * name = XGetAtomName( mydisplay,Event.xproperty.atom );
	    int    wm = vo_wm_Unknown;
	    
	    if ( !name ) break;
	    
//          fprintf(stderr,"[ws] PropertyNotify ( 0x%x ) %s ( 0x%x )\n",vo_window,name,Event.xproperty.atom );
	      
	    XFree( name );
	   }
	   break;
     }
  }
  return ret;
}

void vo_x11_sizehint( int x, int y, int width, int height, int max )
{
 vo_hint.flags=PPosition | PSize | PWinGravity;
 vo_hint.x=x; vo_hint.y=y; vo_hint.width=width; vo_hint.height=height;
 if ( max )
  {
   vo_hint.max_width=width; vo_hint.max_height=height;
   vo_hint.flags|=PMaxSize;
  } else { vo_hint.max_width=0; vo_hint.max_height=0; }
 vo_hint.win_gravity=StaticGravity;
 XSetWMNormalHints( mDisplay,vo_window,&vo_hint );
}

void vo_x11_setlayer( int layer )
{
 Atom            type,arg1,arg2;
 int             format;
 unsigned long   nitems, bytesafter;
 Atom *          args = NULL;
 int i;
 
 if ( WinID >= 0 ) return;
 
 switch ( vo_wm_type )
 { case vo_wm_Layered:
  {
    XClientMessageEvent xev;
    memset(&xev, 0, sizeof(xev));
    xev.type = ClientMessage;
    xev.display= mDisplay;
    xev.window = vo_window;
    xev.message_type = XInternAtom(mDisplay, "_WIN_LAYER", False);
    xev.format = 32;
    xev.data.l[0] = layer?ice_layer:WIN_LAYER_NORMAL; // if not fullscreen, stay on layer "Normal"
    xev.data.l[1] = CurrentTime;
    mp_dbg( MSGT_VO,MSGL_STATUS,"[x11] Layered style stay on top ( layer %d ).\n",xev.data.l[0] );
    XSendEvent(mDisplay, mRootWin, False, SubstructureNotifyMask, (XEvent *) &xev);
    break;
  }
  case vo_wm_NetWM:
  {
   XClientMessageEvent  xev;
   mp_dbg( MSGT_VO,MSGL_STATUS,"[x11] NET style stay on top ( layer %d ).\n",layer );

   memset( &xev,0,sizeof( xev ) );
   xev.type=ClientMessage;
   xev.message_type=XInternAtom( mDisplay,"_NET_WM_STATE",False );
   xev.display=mDisplay;
   xev.window=vo_window;
   xev.format=32;
   xev.data.l[0]=layer;
   
   if (net_wm_support & SUPPORT_FULLSCREEN)
   {
     xev.data.l[1]=XInternAtom( mDisplay,"_NET_WM_STATE_FULLSCREEN",False );
     XSendEvent( mDisplay,mRootWin,False,SubstructureRedirectMask,(XEvent*)&xev );
   } else
   if (net_wm_support & SUPPORT_STAYS_ON_TOP)
   {
     xev.data.l[1]=XInternAtom( mDisplay,"_NET_WM_STATE_STAYS_ON_TOP",False );
     XSendEvent( mDisplay,mRootWin,False,SubstructureRedirectMask,(XEvent*)&xev );
   } else
   if (net_wm_support & SUPPORT_ABOVE)
   {
     xev.data.l[1]=XInternAtom( mDisplay,"_NET_WM_STATE_ABOVE",False );
     XSendEvent( mDisplay,mRootWin,False,SubstructureRedirectMask,(XEvent*)&xev );
   }
  }
 }
}

void vo_x11_fullscreen( void )
{
 int x,y,w,h;

 if ( WinID >= 0 ) return;

 // window manager could be changed during play
 vo_wm_type=vo_wm_detect();
 
 if ( vo_fs ){
   // fs->win
   if(vo_dwidth != vo_screenwidth && vo_dheight != vo_screenheight) return;
   vo_fs=VO_FALSE;
   x=vo_old_x; y=vo_old_y; w=vo_old_width; h=vo_old_height;
 } else {
   // win->fs
   if(vo_old_width && 
     (vo_dwidth==vo_screenwidth && vo_dwidth!=vo_old_width) &&
     (vo_dheight==vo_screenheight && vo_dheight!=vo_old_height) ) return;
   vo_fs=VO_TRUE;
   vo_old_x=vo_dx; vo_old_y=vo_dy; vo_old_width=vo_dwidth; vo_old_height=vo_dheight;
   x=0; y=0; w=vo_screenwidth; h=vo_screenheight;
 }
 if (! (net_wm_support & SUPPORT_FULLSCREEN))
 {
   vo_x11_decoration( mDisplay,vo_window,(vo_fs) ? 0 : 1 );
   vo_x11_sizehint( x,y,w,h,0 );
 }
 vo_x11_setlayer( vo_fs );
 if (! (net_wm_support & SUPPORT_FULLSCREEN))
 {
   if(vo_wm_type==vo_wm_Unknown && !(vo_fsmode&16))
  //     XUnmapWindow( mDisplay,vo_window );  // required for MWM
      XWithdrawWindow(mDisplay,vo_window,mScreen);
   XMoveResizeWindow( mDisplay,vo_window,x,y,w,h );
 }
#ifdef HAVE_XINERAMA
 vo_x11_xinerama_move(mDisplay,vo_window);
#endif
 XMapRaised( mDisplay,vo_window );
 XRaiseWindow( mDisplay,vo_window );
 XFlush( mDisplay );
}

void saver_on(Display *mDisplay) {

#ifdef HAVE_XDPMS
    int nothing;
    if (dpms_disabled)
    {
	if (DPMSQueryExtension(mDisplay, &nothing, &nothing))
	{
	    if (!DPMSEnable(mDisplay)) {  // restoring power saving settings
                mp_msg(MSGT_VO,MSGL_WARN,"DPMS not available?\n");
            } else {
                // DPMS does not seem to be enabled unless we call DPMSInfo
	        BOOL onoff;
        	CARD16 state;
		DPMSForceLevel(mDisplay, DPMSModeOn);
        	DPMSInfo(mDisplay, &state, &onoff);
                if (onoff) {
	            mp_msg(MSGT_VO,MSGL_INFO,"Successfully enabled DPMS\n");
                } else {
	            mp_msg(MSGT_VO,MSGL_WARN,"Could not enable DPMS\n");
                }
            }
	}
	dpms_disabled=0;
    }
#endif

    if (timeout_save)
    {
	int dummy, interval, prefer_blank, allow_exp;
	XGetScreenSaver(mDisplay, &dummy, &interval, &prefer_blank, &allow_exp);
	XSetScreenSaver(mDisplay, timeout_save, interval, prefer_blank, allow_exp);
	XGetScreenSaver(mDisplay, &timeout_save, &interval, &prefer_blank, &allow_exp);
	timeout_save=0;
    }

    if (xscreensaver_was_running && stop_xscreensaver) {
	system("cd /; xscreensaver -no-splash &");
	xscreensaver_was_running = 0;
    }
    if (kdescreensaver_was_running && stop_xscreensaver) {
	system("dcop kdesktop KScreensaverIface enable true 2>/dev/null >/dev/null");
	kdescreensaver_was_running = 0;
    }


}

void saver_off(Display *mDisplay) {

    int interval, prefer_blank, allow_exp;
#ifdef HAVE_XDPMS
    int nothing;

    if (DPMSQueryExtension(mDisplay, &nothing, &nothing))
    {
	BOOL onoff;
	CARD16 state;
	DPMSInfo(mDisplay, &state, &onoff);
	if (onoff)
	{
           Status stat;
	    mp_msg(MSGT_VO,MSGL_INFO,"Disabling DPMS\n");
	    dpms_disabled=1;
	    stat = DPMSDisable(mDisplay);  // monitor powersave off
            mp_msg(MSGT_VO,MSGL_V,"DPMSDisable stat: %d\n", stat);
	}
    }
#endif
    if (!timeout_save) {
	XGetScreenSaver(mDisplay, &timeout_save, &interval, &prefer_blank, &allow_exp);
	if (timeout_save)
	    XSetScreenSaver(mDisplay, 0, interval, prefer_blank, allow_exp);
    }
		    // turning off screensaver
    if (stop_xscreensaver && !xscreensaver_was_running)
    {
      xscreensaver_was_running = (system("xscreensaver-command -version 2>/dev/null >/dev/null")==0);
      if (xscreensaver_was_running)
	 system("xscreensaver-command -exit 2>/dev/null >/dev/null");    
    }
    if (stop_xscreensaver && !kdescreensaver_was_running)
    {
      kdescreensaver_was_running=(system("dcop kdesktop KScreensaverIface isEnabled 2>/dev/null | sed 's/1/true/g' | grep true 2>/dev/null >/dev/null")==0);
      if (kdescreensaver_was_running)
	  system("dcop kdesktop KScreensaverIface enable false 2>/dev/null >/dev/null");
    }
}

static XErrorHandler old_handler = NULL;
static int selectinput_err = 0;
static int x11_selectinput_errorhandler(Display *display, XErrorEvent *event)
{
	if (event->error_code == BadAccess) {
		selectinput_err = 1;
		mp_msg(MSGT_VO, MSGL_ERR, "X11 error : BadAccess during XSelectInput Call\n");
		mp_msg(MSGT_VO, MSGL_ERR, "X11 error : The 'ButtonPressMask' mask of specified window has probably already used by another appication(see man XSelectInput) \n");
		/* If you think mplayer should shutdown with this error, comments out following line */
		return 0;
	}
	if (old_handler != NULL) old_handler(display, event);
	else x11_errorhandler(display, event);
	return 0;
}

void vo_x11_selectinput_witherr(Display *display, Window w, long event_mask)
{
	XSync(display, False);
	old_handler = XSetErrorHandler(x11_selectinput_errorhandler);
	selectinput_err = 0;
	XSelectInput(display, w, event_mask);
	XSync(display, False);
	XSetErrorHandler(old_handler);
	if (selectinput_err) {
		mp_msg(MSGT_VO, MSGL_ERR, "X11 error : Mplayer discards mouse control and retry XSelectInput...\n");
		XSelectInput(display, w, event_mask & (~(ButtonPressMask | ButtonReleaseMask | PointerMotionMask)) );
	}
}

#ifdef HAVE_XINERAMA
void vo_x11_xinerama_move(Display *dsp, Window w)
{
	if(XineramaIsActive(dsp))
	{
		 /* printf("XXXX Xinerama screen: x: %hd y: %hd\n",xinerama_x,xinerama_y); */
		XMoveWindow(dsp,w,xinerama_x,xinerama_y);
	}
}
#endif

#ifdef HAVE_XF86VM
void vo_vm_switch(uint32_t X, uint32_t Y, int* modeline_width, int* modeline_height)
{
    unsigned int vm_event, vm_error;
    unsigned int vm_ver, vm_rev;
    int i,j,have_vm=0;

    int modecount;
    
    if (XF86VidModeQueryExtension(mDisplay, &vm_event, &vm_error)) {
      XF86VidModeQueryVersion(mDisplay, &vm_ver, &vm_rev);
      mp_msg(MSGT_VO,MSGL_V,"XF86VidMode Extension v%i.%i\n", vm_ver, vm_rev);
      have_vm=1;
    } else
      mp_msg(MSGT_VO,MSGL_WARN,"XF86VidMode Extenstion not available.\n");

    if (have_vm) {
      if (vidmodes==NULL)
        XF86VidModeGetAllModeLines(mDisplay,mScreen,&modecount,&vidmodes);
      j=0;
      *modeline_width=vidmodes[0]->hdisplay;
      *modeline_height=vidmodes[0]->vdisplay;
      
      for (i=1; i<modecount; i++)
        if ((vidmodes[i]->hdisplay >= X) && (vidmodes[i]->vdisplay >= Y))
          if ( (vidmodes[i]->hdisplay <= *modeline_width ) && (vidmodes[i]->vdisplay <= *modeline_height) )
	    {
	      *modeline_width=vidmodes[i]->hdisplay;
	      *modeline_height=vidmodes[i]->vdisplay;
	      j=i;
	    }
      
      mp_msg(MSGT_VO,MSGL_INFO,"XF86VM: Selected video mode %dx%d for image size %dx%d.\n",*modeline_width, *modeline_height, X, Y);
      XF86VidModeLockModeSwitch(mDisplay,mScreen,0);
      XF86VidModeSwitchToMode(mDisplay,mScreen,vidmodes[j]);
      XF86VidModeSwitchToMode(mDisplay,mScreen,vidmodes[j]);
      X=(vo_screenwidth-*modeline_width)/2;
      Y=(vo_screenheight-*modeline_height)/2;
      XF86VidModeSetViewPort(mDisplay,mScreen,X,Y);
    }
}

void vo_vm_close(Display *dpy)
{
 #ifdef HAVE_NEW_GUI
        if (vidmodes!=NULL && vo_window != None)
 #else
        if (vidmodes!=NULL)
 #endif
         {
           int i, modecount;
           int screen; screen=DefaultScreen( dpy );

           free(vidmodes); vidmodes=NULL;
           XF86VidModeGetAllModeLines(mDisplay,mScreen,&modecount,&vidmodes);
           for (i=0; i<modecount; i++)
             if ((vidmodes[i]->hdisplay == vo_screenwidth) && (vidmodes[i]->vdisplay == vo_screenheight)) 
               { 
                 mp_msg(MSGT_VO,MSGL_INFO,"\nReturning to original mode %dx%d\n", vo_screenwidth, vo_screenheight);
                 break;
               }

           XF86VidModeSwitchToMode(dpy,screen,vidmodes[i]);
           XF86VidModeSwitchToMode(dpy,screen,vidmodes[i]);
           free(vidmodes);
         }
}
#endif

#endif	/* X11_FULLSCREEN */


/*
 * Scan the available visuals on this Display/Screen.  Try to find
 * the 'best' available TrueColor visual that has a decent color
 * depth (at least 15bit).  If there are multiple visuals with depth
 * >= 15bit, we prefer visuals with a smaller color depth.
 */
int vo_find_depth_from_visuals(Display *dpy, int screen, Visual **visual_return)
{
  XVisualInfo visual_tmpl;
  XVisualInfo *visuals;
  int nvisuals, i;
  int bestvisual = -1;
  int bestvisual_depth = -1;

  visual_tmpl.screen = screen;
  visual_tmpl.class = TrueColor;
  visuals = XGetVisualInfo(dpy,
			   VisualScreenMask | VisualClassMask, &visual_tmpl,
			   &nvisuals);
  if (visuals != NULL) {
    for (i = 0; i < nvisuals; i++) {
      mp_msg(MSGT_VO,MSGL_V,"vo: X11 truecolor visual %#x, depth %d, R:%lX G:%lX B:%lX\n",
	       visuals[i].visualid, visuals[i].depth,
	       visuals[i].red_mask, visuals[i].green_mask,
	       visuals[i].blue_mask);
      /*
       * save the visual index and it's depth, if this is the first
       * truecolor visul, or a visual that is 'preferred' over the
       * previous 'best' visual
       */
      if (bestvisual_depth == -1
	  || (visuals[i].depth >= 15 
	      && (   visuals[i].depth < bestvisual_depth
		  || bestvisual_depth < 15))) {
	bestvisual = i;
	bestvisual_depth = visuals[i].depth;
      }
    }

    if (bestvisual != -1 && visual_return != NULL)
      *visual_return = visuals[bestvisual].visual;

    XFree(visuals);
  }
  return bestvisual_depth;
}


static Colormap cmap = None;
static XColor cols[256];
static int cm_size, red_mask, green_mask, blue_mask;


Colormap vo_x11_create_colormap(XVisualInfo *vinfo)
{
	unsigned k, r, g, b, ru, gu, bu, m, rv, gv, bv, rvu, gvu, bvu;

	if (vinfo->class != DirectColor)
	 return XCreateColormap(mDisplay, mRootWin, vinfo->visual, AllocNone);

	/* can this function get called twice or more? */
	if (cmap) return cmap;
	cm_size = vinfo->colormap_size;
	red_mask = vinfo->red_mask;
	green_mask = vinfo->green_mask;
	blue_mask = vinfo->blue_mask;
	ru = (red_mask&(red_mask-1))^red_mask;
	gu = (green_mask&(green_mask-1))^green_mask;
	bu = (blue_mask&(blue_mask-1))^blue_mask;
	rvu = 65536ull*ru/(red_mask + ru);
	gvu = 65536ull*gu/(green_mask + gu);
	bvu = 65536ull*bu/(blue_mask + bu);
	r = g = b = 0;
	rv = gv = bv = 0;
	m = DoRed|DoGreen|DoBlue;
	for (k = 0; k < cm_size; k++) {
		int t;
		cols[k].pixel = r|g|b;
		cols[k].red = rv;
		cols[k].green = gv;
		cols[k].blue = bv;
		cols[k].flags = m;
		t = (r + ru) & red_mask; if (t < r) m &= ~DoRed; r = t;
		t = (g + gu) & green_mask; if (t < g) m &= ~DoGreen; g = t;
		t = (b + bu) & blue_mask; if (t < b) m &= ~DoBlue; b = t;
		rv += rvu;
		gv += gvu;
		bv += bvu;
	}
	cmap = XCreateColormap(mDisplay, mRootWin, vinfo->visual, AllocAll);
	XStoreColors(mDisplay, cmap, cols, cm_size);
	return cmap;
}

/*
 * Via colormaps/gamma ramps we can do gamma, brightness, contrast,
 * hue and red/green/blue intensity, but we cannot do saturation.
 * Currently only gamma, brightness and contrast are implemented.
 * Is there sufficient interest for hue and/or red/green/blue intensity?
 */
/* these values have range [-100,100] and are initially 0 */
static int vo_gamma = 0;
static int vo_brightness = 0;
static int vo_contrast = 0;


uint32_t vo_x11_set_equalizer(char *name, int value)
{
	float gamma, brightness, contrast;
	float rf, gf, bf;
	int k;

	/*
	 * IMPLEMENTME: consider using XF86VidModeSetGammaRamp in the case
	 * of TrueColor-ed window but be careful:
	 * unlike the colormaps, which are private for the X client
	 * who created them and thus automatically destroyed on client
	 * disconnect, this gamma ramp is a system-wide (X-server-wide)
	 * setting and _must_ be restored before the process exit.
	 * Unforunately when the process crashes (or get killed
	 * for some reason) it is impossible to restore the setting,
	 * and such behaviour could be rather annoying for the users.
	 */
	if (cmap == None) return VO_NOTAVAIL;

	if (!strcasecmp(name, "brightness")) vo_brightness = value;
	else if (!strcasecmp(name, "contrast")) vo_contrast = value;
	else if (!strcasecmp(name, "gamma")) vo_gamma = value;
	else return VO_NOTIMPL;
	
	brightness = 0.01*vo_brightness;
	contrast = tan(0.0095*(vo_contrast+100)*M_PI/4);
	gamma = pow(2, -0.02*vo_gamma);

	rf = (float)((red_mask & (red_mask - 1)) ^ red_mask)/red_mask;
	gf = (float)((green_mask & (green_mask - 1)) ^ green_mask)/green_mask;
	bf = (float)((blue_mask & (blue_mask - 1)) ^ blue_mask)/blue_mask;

	/* now recalculate the colormap using the newly set value */
	for (k = 0; k < cm_size; k++) {
		float s;

		s = pow(rf*k, gamma);
		s = (s - 0.5)*contrast + 0.5;
		s += brightness;
		if (s < 0) s = 0;
		if (s > 1) s = 1;
		cols[k].red = (unsigned short)(s * 65535);

		s = pow(gf*k, gamma);
		s = (s - 0.5)*contrast + 0.5;
		s += brightness;
		if (s < 0) s = 0;
		if (s > 1) s = 1;
		cols[k].green = (unsigned short)(s * 65535);

		s = pow(bf*k, gamma);
		s = (s - 0.5)*contrast + 0.5;
		s += brightness;
		if (s < 0) s = 0;
		if (s > 1) s = 1;
		cols[k].blue = (unsigned short)(s * 65535);
	}
		
	XStoreColors(mDisplay, cmap, cols, cm_size);
	XFlush(mDisplay);
	return VO_TRUE;
}

uint32_t vo_x11_get_equalizer(char *name, int *value)
{
	if (cmap == None) return VO_NOTAVAIL;
	if (!strcasecmp(name, "brightness")) *value = vo_brightness;
	else if (!strcasecmp(name, "contrast")) *value = vo_contrast;
	else if (!strcasecmp(name, "gamma")) *value = vo_gamma;
	else return VO_NOTIMPL;
	return VO_TRUE;
}


