// xdiskusage.C

// Display disk usage in a window.  

#if defined(__bsdi__)
#define DF_COMMAND "df"
#define DU_COMMAND "du -x"
#elif defined(SVR4)
#define DF_COMMAND "df -k"
#define DU_COMMAND "du -kd"
#else
#define DF_COMMAND "df -k"
#define DU_COMMAND "du -kx"
#endif

const char* copyright = 
"Disk Usage Display\n"
"Copyright (C) 1998 Bill Spitzak    spitzak@d2.com\n"
"Based on xdu by Phillip C. Dykstra\n"
"\n"
"This program is free software; you can redistribute it and/or modify "
"it under the terms of the GNU General Public License as published by "
"the Free Software Foundation; either version 2 of the License, or "
"(at your option) any later version.\n"
"\n"
"This program is distributed in the hope that it will be useful, "
"but WITHOUT ANY WARRANTY; without even the implied warranty of "
"MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the "
"GNU General Public License for more details.\n"
"\n"
"You should have received a copy of the GNU General Public License "
"along with this software; if not, write to the Free Software "
"Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 "
"USA.";

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

#include "panels.H"
#include <FL/fl_draw.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Menu_Button.H>
#include <FL/fl_ask.H>

typedef unsigned long ulong;

// turn number of K into user-friendly text:
const char* formatk(ulong k) {
  static char buffer[10];
  if (k >= 1024*1024) sprintf(buffer,"%.4gG",(double)k/(1024*1024));
  else if (k >= 1024) sprintf(buffer,"%.4gM",(double)k/1024);
  else sprintf(buffer,"%ldK",k);
  return buffer;
}

struct Disk {
  const char* mount;
  ulong k;
  ulong used;
  Disk* next;
};

Disk* firstdisk;

int main(int argc, char**argv) {

  Fl::args(argc,argv);
  make_diskchooser();

  reload_cb(0,0);

  disk_chooser->show(argc,argv);
  return Fl::run();
}

void reload_cb(Fl_Button*, void*) {
  FILE* f = popen(DF_COMMAND, "r");
  if (!f) {
    fprintf(stderr,"Can't run df, %s\n", strerror(errno));
    exit(1);
  }
  Disk** pointer = &firstdisk;
  for (;;) {
    char buffer[1024];
    if (!fgets(buffer, 1024, f)) break;
    int n = 0; // number of words
    char* word[10]; // pointer to each word
    for (char* p = buffer; n < 10;) {
      // skip leading whitespace:
      while (*p && isspace(*p)) p++;
      if (!*p) break;
      // skip over the word:
      word[n++] = p;
      while (*p && !isspace(*p)) p++;
      if (!*p) break;
      *p++ = 0;
    }
    if (n < 5 || word[n-1][0] != '/') continue;
    // ok we found a line with a /xyz at the end:
    Disk* d = new Disk;
    d->mount = strdup(word[n-1]);
    d->k = strtol(word[n-5],0,10);
    d->used=strtol(word[n-4],0,10);
    *pointer = d;
    d->next = 0;
    pointer = &d->next;
  }
  pclose(f);

  if (!firstdisk) {
    fl_alert("Something went wrong with df, no disks found.");
  } else {
    disk_browser->clear();
  }

  for (Disk* d = firstdisk; d; d = d->next) {
    char buf[512];
    sprintf(buf, "@b%s\t@r%s\n", d->mount, formatk(d->k));
    disk_browser->add(buf);
  }
  disk_browser->position(0);
}

Fl_Window *copyright_window;
void copyright_cb(Fl_Button*, void*) {
  if (!copyright_window) {
    copyright_window = new Fl_Window(400,270,"Copyright");
    copyright_window->color(FL_WHITE);
    Fl_Box *b = new Fl_Box(20,0,380,270,copyright);
    b->labelsize(10);
    b->align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE|FL_ALIGN_WRAP);
    copyright_window->end();
  }
  copyright_window->hotspot(copyright_window);
  copyright_window->set_non_modal();
  copyright_window->show();
}

struct Node {
  Node* child;
  Node* brother;
  const char* name;
  ulong size;
  ulong ordinal;
};

// default sizes of new windows, changed by user actions on other windows:
int window_w = 600;
int window_h = 480;
int ncols = 5;
#define MAXDEPTH 80

class Display : public Fl_Window {
  void draw();
  int handle(int);
  void resize(int,int,int,int);
  Node* root;
  Node* current_root;
  Node* parents[MAXDEPTH];
  int depth;
  int ncols;
  Fl_Menu_Button menu_button;
  Display(int w, int h, const char* l) : Fl_Window(w,h,l),
    menu_button(0,0,w,h) {}
  void draw_tree(Node* n, int column, ulong row, double scale);
  void print_tree(FILE* f, Node* n, int column, ulong row, double scale, int W, int H);
  void setroot(Node* n, int depth);
public:
  static void root_cb(Fl_Widget*, void*);
  static void out_cb(Fl_Widget*, void*);
  static void in_cb(Fl_Widget*, void*);
  static void next_cb(Fl_Widget*, void*);
  static void previous_cb(Fl_Widget*, void*);
  static void sort_cb(Fl_Widget* o, void*);
  static void columns_cb(Fl_Widget* o, void*);
  static void copy_cb(Fl_Widget* o, void*);
  static void print_cb(Fl_Widget* o, void*);
  static Node* sort(Node* n, int (*compare)(const Node*, const Node*));
  static Display* make(const char*, ulong, ulong, int);
  ~Display();
};

void disk_browser_cb(Fl_Browser*b, void*) {
  int i = b->value();
  if (!i) return;
  Disk* d;
  for (d = firstdisk; i > 1; i--) d = d->next;
  Display::make(d->mount, d->used, d->k, all_files_button->value());
  //b->value(0);
}

#include <FL/filename.H>

void disk_input_cb(Fl_Input* i, void*) {
  // follow all symbolic links...
  char buf[1024]; strncpy(buf, i->value(), 1024);
  for (;;) {
    char *p = (char*)filename_name(buf);
    int i = readlink(buf, p, 1024-(p-buf));
    if (i < 0) {
      if (errno != EINVAL) {
	strcat(buf, ": no such file");
	fl_alert(buf);
	return;
      }
      break;
    }
    if (*p == '/') {memmove(buf, p, i); p = buf;}
    p[i] = 0;
  }
  Display::make(buf, 0, 0, all_files_button->value());
}

void close_cb(Fl_Widget* o, void*) {
  Display* d = (Display*)o;
  delete d;
}

static int cancelled;

void cancel_cb(Fl_Button*, void*) {
  cancelled = 1;
}

void delete_tree(Node* n) {
  while (n) {
    if (n->child) delete_tree(n->child);
    Node* next = n->brother;
    free((void*)n->name);
    delete n;
    n = next;
  }
}

// fill in missing totals by adding all sons:
void fix_tree(Node* n) {
  if (n->size) return;
  ulong total = 0;
  for (Node *x = n->child; x; x = x->brother) total += x->size;
  n->size = total;
}

static int ordinal;

Node* newnode(const char* name, ulong size, Node* parent, Node* & brother) {
  Node* n = new Node;
  n->child = n->brother = 0;
  n->name = strdup(name);
  n->size = size;
  n->ordinal = ++ordinal;
  if (parent->child) {
    brother->brother = n;
  } else {
    parent->child = n;
  }	
  brother = n;
  return n;
}

static Fl_Menu_Item menutable[] = {
  {"in", FL_Right, Display::in_cb},
  {"next", FL_Down, Display::next_cb},
  {"previous", FL_Up, Display::previous_cb},
  {"out", FL_Left, Display::out_cb},
  {"root", '/', Display::root_cb},
  {"sort", 0, 0, 0, FL_SUBMENU},
    {"largest first", 's', Display::sort_cb, (void*)'s'},
    {"smallest first", 'r', Display::sort_cb, (void*)'r'},
    {"alphabetical", 'a', Display::sort_cb, (void*)'a'},
    {"reverse alphabetical", 'z', Display::sort_cb, (void*)'z'},
    {"unsorted", 'u', Display::sort_cb, (void*)'u'},
    {0},
  {"columns", 0, 0, 0, FL_SUBMENU},
    {"2", '2', Display::columns_cb, (void*)2},
    {"3", '3', Display::columns_cb, (void*)3},
    {"4", '4', Display::columns_cb, (void*)4},
    {"5", '5', Display::columns_cb, (void*)5},
    {"6", '6', Display::columns_cb, (void*)6},
    {"7", '7', Display::columns_cb, (void*)7},
    {"8", '8', Display::columns_cb, (void*)8},
    {"9", '9', Display::columns_cb, (void*)9},
    {"10", '0', Display::columns_cb, (void*)10},
    {"11", '1', Display::columns_cb, (void*)11},
    {0},
  {"copy to clipboard", 'c', Display::copy_cb},
  {"print", 'p', Display::print_cb},
  {0}
};

static int largestfirst(const Node* a, const Node* b) {
  return (a->size > b->size) ? -1 : 1;
}

Display* Display::make(const char* path, ulong used, ulong total, int all) {

  cancelled = 0;

  char buffer[1024];
#ifdef __sgi
  // sgi has the -x and -L switches somehow confused.  You cannot turn
  // off descent into mount points (with -x) without turning on descent
  // into symbolic links (at least for a stat()).  At DD all the symbolic
  // links are on the /usr disk so I special case it:
  if (!strcmp(path, "/usr"))
    sprintf(buffer, "du -k%c \"%s\"", all ? 'a' : ' ', path);
  else
#endif
    sprintf(buffer, DU_COMMAND"%c \"%s\"", all ? 'a' : ' ', path);

  FILE* f = popen(buffer,"r");
  if (!f) {
    fl_alert("Problem running du!");
    return 0;
  }

  if (!wait_window) make_wait_window();
  wait_slider->type(used ? FL_HOR_FILL_SLIDER : FL_HOR_SLIDER);
  wait_slider->value(0.0);
  wait_window->show();
  Fl::flush();

  Node* root = new Node;
  root->child = root->brother = 0;
  root->name = strdup(path);
  root->size = 0;
  root->ordinal = 0;
  ordinal = 0;

  Node* lastnode[MAXDEPTH];
  ulong runningtotal;
  ulong totals[MAXDEPTH];
  lastnode[0] = root;
  runningtotal = 0;
  totals[0] = 0;
  int currentdepth = 0;

  while (fgets(buffer, 1024, f)) {
    char* p;

    // null-terminate the line:
    p = buffer+strlen(buffer);
    if (p > buffer && p[-1] == '\n') p[-1] = 0;

    ulong size = strtoul(buffer, &p, 10);
    if (!size) continue;
    while (isspace(*p)) p++;

    // split the path into parts:
    // the first part of path must match pathname (which may have slashes):
    const char* r = path;
    while (*r && *p == *r) {p++; r++;}
    if (*r == '/') r++;
    if (*r) {
      fprintf(stderr,"Unexpected path from du: %s\n", buffer);
      continue;
    }
    if (*p == '/') p++;
    int newdepth = 0;
    const char* parts[MAXDEPTH];
    if (*p) {
      parts[1] = p;
      for (newdepth = 1; newdepth < MAXDEPTH; newdepth++) {
	while (*p && *p != '/') p++;
	if (*p == '/') {*p++ = 0; parts[newdepth+1] = p;}
	else {*p = 0; break;}
      }
    }

    // find out how many of the fields match:
    int match = 0;
    for (; match < newdepth && match < currentdepth; match++) {
      if (strcmp(parts[match+1],lastnode[match+1]->name)) break;
    }

    for (int j = currentdepth; j > match; j--) fix_tree(lastnode[j]);

    if (match == newdepth) {
      Node* p = lastnode[newdepth];
      ulong t = totals[newdepth]+size;
      if ((all || used && !newdepth) && t > runningtotal) {
	// add a dummy node for any unreported files:
	newnode("(files)", t-runningtotal, p, lastnode[newdepth+1]);
//    } else if (t < runningtotal) {
// 	printf("oversize %ld > %s\n", runningtotal-totals[newdepth], buffer);
      }
      p->size = size;
      runningtotal = t;
    } else {
      Node* p = lastnode[match];
      for (int j = match+1; j <= newdepth; j++) {
	totals[j] = runningtotal;
	p = newnode(parts[j], 0, p, lastnode[j]);
      }
      p->size = size;
      runningtotal += size;
    }

    currentdepth = newdepth;
    totals[newdepth] = runningtotal;

    wait_slider->value(used ? (double)runningtotal/used :
		       (double)(ordinal%1024)/1024);
    Fl::check();
    if (cancelled) {
      delete_tree(root);
      pclose(f);
      wait_window->hide();
      return 0;
    }
  }
  pclose(f);
  wait_window->hide();

  if (used > runningtotal) {
    // add a dummy node for missing stuff (probably permission denied errors):
    newnode("(?)", used-runningtotal, root, lastnode[1]);
    runningtotal = used;
  }

  if (total > runningtotal) {
    // add a dummy node for disk free space:
    newnode("(free)", total-runningtotal, root, lastnode[1]);
    runningtotal = total;
  }

  root->size = runningtotal;

  Display* d = new Display(window_w, window_h, root->name);
  d->ncols = ::ncols;
  d->root = d->current_root = sort(root, largestfirst);
  d->resizable(d);
  d->menu_button.menu(menutable);
  d->menu_button.box(FL_NO_BOX);
  d->callback(close_cb);
  d->depth = 0;
  d->show();

  return 0;
}

void Display::draw_tree(Node* n, int column, ulong row, double scale) {
  if (!n || column >= ncols) return;
  int X = (w()-1)*column/ncols;
  int W = (w()-1)*(column+1)/ncols - X;
  for (; n; n = n->brother) {
    int Y,H;
    if (n == current_root) {Y = 0; H = h()-1;}
    else {Y = int(row*scale+.5); H = int((row+n->size)*scale+.5) - Y;}
    if (H > 0) {
      fl_color(FL_WHITE);
      fl_rectf(X,Y,W,H);
      fl_color(FL_BLACK);
      fl_rect(X,Y,W+1,H+1);
      if (H > 20) {
	fl_draw(n->name, X, Y, W, H/2, FL_ALIGN_BOTTOM);
	fl_draw(formatk(n->size), X, Y+H/2, W, H/2, FL_ALIGN_TOP);
      } else if (H > 10) {
	fl_draw(n->name, X, Y, W, H, FL_ALIGN_CENTER);
      }
      draw_tree(n->child, column+1, row, scale);
    }
    if (n == current_root) return;
    row += n->size;
  }
}

void Display::draw() {
  fl_draw_box(box(),0,0,w(),h(),color());
  double scale = (double)(h()-1)/current_root->size;
  fl_font(0,10);
  draw_tree(current_root, 0, 0, scale);
}

int Display::handle(int event) {
  switch (event) {
  case FL_PUSH:
    if (Fl::event_button() != 1) return Fl_Window::handle(event);
    return 1;
  case FL_DRAG:
    return 1;
  case FL_RELEASE:
    break;
  default:
    return Fl_Window::handle(event);
  }
  // okay, it is a button-up, figure out what they clicked:
  int X = Fl::event_x();
  int Y = Fl::event_y();
  if (X < 0 || X >= w() || Y < 0 || Y >= h()) return 1;
  int column = X * ncols / w();
  if (!column) {
    // clicked on left column, go up one
    if (depth) setroot(parents[depth-1],depth-1);
    return 1;
  }
  column += depth;
  double scale = (double)(h()-1)/current_root->size;
  Node* n = current_root;
  int row = 0;
  int d = depth;
  while (d < column) {
    parents[d] = n;
    n = n->child;
    for (; ; n = n->brother) {
      if (!n) return 1;
      int Y1 = int((row+n->size)*scale+.5);
      if (Y < Y1) break;
      row += n->size;
    }
    d++;
  }
  setroot(n,d);
  return 1;
}

void Display::setroot(Node* n, int newdepth) {
  if (n == current_root) return;
  current_root = n;
  depth = newdepth;
  char buffer[1024];
  char* p = buffer;
  for (int i = 0; i < depth; i++) {
    const char* q = parents[i]->name;
    while (*q) *p++ = *q++;
    if (p[-1] != '/') *p++ = '/';
  }
  strcpy(p,n->name);
  label(buffer);
  redraw();
}

void Display::copy_cb(Fl_Widget* o, void*) {
  Display* d = (Display*)(o->parent());
  char buffer[1024];
  char* p = buffer;
  for (int i = 0; i < d->depth; i++) {
    const char* q = d->parents[i]->name;
    while (*q) *p++ = *q++;
    if (p[-1] != '/') *p++ = '/';
  }
  strcpy(p,d->current_root->name);
  Fl::selection(*d, buffer, strlen(buffer));
}
  
Display::~Display() {
  delete_tree(root);
}

void Display::root_cb(Fl_Widget* o, void*) {
  Display* d = (Display*)(o->parent());
  d->setroot(d->root, 0);
}
void Display::out_cb(Fl_Widget* o, void*) {
  Display* d = (Display*)(o->parent());
  if (d->depth) d->setroot(d->parents[d->depth-1], d->depth-1);
}
void Display::in_cb(Fl_Widget* o, void*) {
  Display* d = (Display*)(o->parent());
  if (d->current_root->child) {
    d->parents[d->depth] = d->current_root;
    d->setroot(d->current_root->child, d->depth+1);
  }
}
void Display::next_cb(Fl_Widget* o, void*) {
  Display* d = (Display*)(o->parent());
  if (d->current_root->brother)
    d->setroot(d->current_root->brother, d->depth);
}
void Display::previous_cb(Fl_Widget* o, void*) {
  Display* d = (Display*)(o->parent());
  if (!d->depth) return;
  Node* n = d->parents[d->depth-1]->child;
  while (n) {
    if (n->brother == d->current_root) {
      d->setroot(n, d->depth);
      return;
    }
    n = n->brother;
  }
}

static int smallestfirst(const Node* a, const Node* b) {
  return (a->size < b->size) ? -1 : 1;
}
static int alphabetical(const Node* a, const Node* b) {
  return strcmp(a->name, b->name);
}
static int zalphabetical(const Node* a, const Node* b) {
  return -strcmp(a->name, b->name);
}
static int unsorted(const Node* a, const Node* b) {
  return (a->ordinal < b->ordinal) ? -1 : 1;
}

Node* Display::sort(Node* n, int (*compare)(const Node*, const Node*)) {
  if (!n) return 0;
  Node* head = 0;
  while (n) {
    Node* n1 = n->brother;
    Node** p = &head;
    while (*p) {if (compare(n, *p) < 0) break; p = &(*p)->brother;}
    n->brother = *p;
    *p = n;
    n->child = sort(n->child, compare);
    n = n1;
  }
  return head;
}

void Display::sort_cb(Fl_Widget* o, void*v) {
  Display* d = (Display*)(o->parent());
  int (*compare)(const Node*, const Node*);
  switch ((int)v) {
  case 's': compare = largestfirst; break;
  case 'r': compare = smallestfirst; break;
  case 'a': compare = alphabetical; break;
  case 'z': compare = zalphabetical; break;
  default: compare = unsorted; break;
  }
  d->root = sort(d->root, compare);
  d->redraw();
}

void Display::columns_cb(Fl_Widget* o, void*v) {
  Display* d = (Display*)(o->parent());
  int n = (int)v;
  if (n == d->ncols) return;
  ::ncols = d->ncols = n;
  d->redraw();
}

void Display::resize(int X, int Y, int W, int H) {
  window_w = W;
  window_h = H;
  Fl_Window::resize(X,Y,W,H);
}

////////////////////////////////////////////////////////////////

void Display::print_tree(FILE*f,Node* n, int column, ulong row, double scale,
			 int bboxw, int bboxh) {
  if (!n || column >= ncols) return;
  int X = bboxw*column/ncols;
  int W = bboxw*(column+1)/ncols - X;
  for (; n; n = n->brother) {
    double Y,H;
    if (n == current_root) {Y = 0; H = bboxh;}
    else {Y = row*scale; H = n->size*scale;}
    if (H >= 1.0) {
      fprintf(f, "%d %g %d %g rect", X, -Y, W, -H);
      if (H > 20) {
	fprintf(f, " %d %g moveto (%s) show", X+5, -Y-H/2, n->name);
	fprintf(f, " %d %g moveto (%s) show", X+5, -Y-H/2-10,formatk(n->size));
      } else if (H > 10) {
	fprintf(f, " %d %g moveto (%s) show", X+5, -Y-H/2-4, n->name);
      }
      fprintf(f, "\n");
      print_tree(f, n->child, column+1, row, scale, bboxw, bboxh);
    }
    if (n == current_root) return;
    row += n->size;
  }
}

void Display::print_cb(Fl_Widget* o, void*) {
  Display* d = (Display*)(o->parent());
  if (!print_panel) make_print_panel();
  print_panel->show();
  for (;;) {
    if (!print_panel->shown()) return;
    Fl_Widget* o = Fl::readqueue();
    if (o) {
      if (o == print_ok_button) break;
    } else {
      Fl::wait();
    }
  }
  print_panel->hide();
  FILE *f;
  if (print_file_button->value()) {
    f = fopen(print_file_input->value(), "w");
  } else {
    f = popen(print_command_input->value(), "w");
  }
  if (!f) {
    fl_alert("Can't open printer output stream");
    return;
  }
  fprintf(f, "%%!PS-Adobe-2.0\n");
  int W = 7*72+36;
  int H = 10*72;
  if (!print_portrait_button->value()) {int t = W; W = H; H = t;}
  int X = 0;
  int Y = 0;
  if (!fill_page_button->value()) {
    if (d->w()*H > d->h()*W) {int t = d->h()*W/d->w(); Y = (H-t)/2; H = t;}
    else {int t = d->w()*H/d->h(); X = (W-t)/2; W = t;}
  }
  if (print_portrait_button->value())
    fprintf(f, "%%%%BoundingBox: 36 36 %d %d\n",36+X+W,36+Y+H);
  else
    fprintf(f, "%%%%BoundingBox: 36 36 %d %d\n",36+Y+H,36+X+W);
  fprintf(f, "/rect {4 2 roll moveto dup 0 exch rlineto exch 0 rlineto neg 0 exch rlineto closepath stroke} bind def\n");
  fprintf(f, "/pagelevel save def\n");
  fprintf(f, "/Helvetica findfont 10 scalefont setfont\n");
  fprintf(f, "0 setlinewidth\n");
  if (print_portrait_button->value())
    fprintf(f, "%d %d translate\n", 36+X, 36+Y+H);
  else
    fprintf(f, "%d %d translate 90 rotate\n", 36+Y, 36+X);
  double scale = (double)H/d->current_root->size;
  d->print_tree(f, d->current_root, 0, 0, scale, W, H);
  fprintf(f,"showpage\npagelevel restore\n%%%%EOF\n");
  if (print_file_button->value()) {
    fclose(f);
  } else {
    pclose(f);
  }
}