/* vi:set ts=8 sts=4 sw=4 noet list:vi
 *
 * VIM - Vi IMproved	by Bram Moolenaar
 *                      Popup List by Marko Mahnič
 *
 * Do ":help uganda"  in Vim to read copying and usage conditions.
 * Do ":help credits" in Vim to see a list of people who contributed.
 * See README.txt for an overview of the Vim source code.
 */

/*
 * popuplst.c: Popup list (PULS)
 *
 * Copyright © 2011 Marko Mahnič.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/*                 WORK IN PROGRESS                   */

#include "vim.h"

#if defined(FEAT_POPUPLIST)

#define EQUALS(a, b)  (0 == strcmp(a, b))
#define EQUALSN(a, b, n)  (0 == strncmp(a, b, n))
#define STARTSWITH(str, prefix)  (0 == strncmp(str, prefix, strlen(prefix)))

/* HACK from eval.c */
static dictitem_T dumdi;
static char_u blankline[] = "";
#define DI2HIKEY(di) ((di)->di_key)
#define HIKEY2DI(p)  ((dictitem_T *)(p - (dumdi.di_key - (char_u *)&dumdi)))
#define HI2DI(hi)     HIKEY2DI((hi)->hi_key)

/* Linux defines __compar_fn_t. Not sure about others */
#define LT_SORT_UP	-1
#define GT_SORT_UP	-LT_SORT_UP
#define LT_SORT_DOWN	-LT_SORT_UP
#define GT_SORT_DOWN	-LT_SORT_DOWN
#define STRCMP_SORT_UP  1
#define STRCMP_SORT_DOWN  -1
typedef int (*__puls_compar_fn_t)(const void* a, const void* b);

#if 1
list_T PULSLOG;
    static void
pulslog(char* fmt, ...)
{
    va_list	ap;
    int		str_l;
    static char buf[128+1];
    listitem_T	*item;

    va_start(ap, fmt);
    str_l = vim_vsnprintf(&buf[0], 128, fmt, ap, NULL);
    va_end(ap);

    item = (listitem_T*) alloc(sizeof(listitem_T));
    item->li_tv.v_type = VAR_STRING;
    item->li_tv.vval.v_string = vim_strsave(buf);

    /* from list_append */
    if (PULSLOG.lv_last == NULL)
    {
	PULSLOG.lv_first = item;
	PULSLOG.lv_last = item;
	item->li_prev = NULL;
    }
    else
    {
	PULSLOG.lv_last->li_next = item;
	item->li_prev = PULSLOG.lv_last;
	PULSLOG.lv_last = item;
    }
    ++PULSLOG.lv_len;
    item->li_next = NULL;
}
#define LOG( X )   pulslog X
#else
#define LOG( X )
#endif

    static int
limit_value(value, vmin, vmax)
    int value;
    int vmin;
    int vmax;
{
    if (value < vmin)
	value = vmin;
    if (value > vmax)
	value = vmax;
    return value;
}

/*
 * Search for needle in haystack, ignoring case.
 * Doesn't work for multi-byte characters.
 */
    static char*
_stristr(haystack, needle)
    const char* haystack;
    const char* needle;
{
    char* p;
    int hs = STRLEN(haystack);
    int ns = STRLEN(needle);
    if (hs < ns)
	return NULL;

    hs -= ns;
    p = (char*) haystack;
    while (hs >= 0)
    {
	if (0 == STRNICMP(p, needle, ns))
	    return p;
	p++;
	hs--;
    }

    return NULL;
}

    static void
_str_assign(dest, src)
    char_u** dest;
    char_u* src;
{
    if (! dest)
	return;
    if (*dest && src && STRLEN(*dest) == STRLEN(src))
    {
	STRCPY(*dest, src);
	return;
    }
    if (*dest)
	vim_free(*dest);
    if (src)
	*dest = vim_strsave(src);
    else
	*dest = NULL;
}

    static void
_str_free(str)
    char_u** str;
{
    if (!str || !*str)
	return;
    vim_free(*str);
    *str = NULL;
}

#include "popupls_.ci" /* created by mmoocc.py from class definitions in [ooc] blocks */
#include "puls_tw.c"

/*
 * Providers:
 *  list of C strings
 *  list of vim strings/arbitrary objects
 *  buffer list
 *  quickfix/location list
 *  menu tree
 *  filesystem tree
 *  filesystem flat view (files in subdirs)
 * Interface:
 *  char_u* get_text(i)
 *     - returns a pointer to item text, NTS
 *     - the pointer can reference a temporary value
 *  int select_item(i)
 *     - an item was selected by pressing Enter
 *     - if item contains subitems (hierarchical) the function can return 1 (redisplay, new items)
 *     - the provider manages level and path
 *	    int level;			the current level in a hierarchical dataset
 *	    int path[MAX_LEVELS];	the selected item in a hierarchical dataset
 *     - we need a function to move up in the hierarchy
 *	    int select_parent()	returns 1 to redisplay the list (new items)
 *     - we may want to do something with the path
 *	    char_u* get_path_text()
 *  It may take a longer time for the provider to obtain all the items. In this case 
 *  the PULS could poll the provider for new items in fixed intervals.
 *	int get_more_items()
 *	    - 0 - no more items
 *	    - 1 - items appended to the list
 *	    - 2 - the list was modified (maybe it was sorted)
 *  The provider could respond to some actions:
 *	int exec_action(char_u* action, int current)
 *
 *  The items in the list could be marked. The porvider could do something with
 *  the marked items.
 */

/* [ooc]
 *
  class DictIterator [dicti]
  {
    dict_T*	dict;
    hashitem_T*	current;
    int		todo;
    void	init();
    void	destroy();
    dictitem_T* begin(dict_T* dict);
    dictitem_T* next();
  }
*/

    static void
_dicti_init(_self)
    void* _self;
{
    METHOD(DictIterator, init);
    self->dict = NULL;
    self->current = NULL;
}

    static void
_dicti_destroy(_self)
    void* _self;
{
    METHOD(DictIterator, destroy);
    self->dict = NULL;
    self->current = NULL;
    END_DESTROY(DictIterator);
}

    static dictitem_T*
_dicti_begin(_self, dict)
    void* _self;
    dict_T* dict;
{
    METHOD(DictIterator, begin);
    self->dict = dict;
    self->todo = (int)dict->dv_hashtab.ht_used;
    self->current = dict->dv_hashtab.ht_array;
    if (self->todo < 1)
	return NULL;
    if (HASHITEM_EMPTY(self->current))
	return self->op->next(self);

    --self->todo;
    return HI2DI(self->current);
}

    static dictitem_T*
_dicti_next(_self)
    void* _self;
{
    METHOD(DictIterator, next);
    if (self->todo < 1)
	return NULL;
    ++self->current;
    while (HASHITEM_EMPTY(self->current))
	++self->current;

    --self->todo;
    return HI2DI(self->current);
}

/* [ooc]
 *
  const ITEM_SHARED = 0x01;
  const ITEM_MARKED = 0x02;
  const ITEM_TITLE  = 0x04;
  struct PopupItem [ppit]
  {
    void*	data; // additional data for the item
    char_u*	text;
    // TODO: when the list is regenerated, the flags are lost! (marked, titleitem)
    ushort	flags; // cached; marked; titleitem; ...
    ushort	filter_start;
    ushort	filter_length;
    ushort	filter_score;
    void	init();
    void	destroy();
  };
  typedef int (*PopupItemCompare_Fn)(PopupItem_T* a, PopupItem_T* b);
*/

    static void
_ppit_init(_self)
    void* _self;
{
    METHOD(PopupItem, init);
    self->data		= NULL;
    self->text		= NULL;
    self->filter_start	= 0;
    self->filter_length	= 65535; /* assume NUL terminated string */
    self->filter_score	= 1;
    self->flags		= 0;
}

    static void
_ppit_destroy(_self)
    void* _self;
{
    METHOD(PopupItem, destroy);
    if (self->text && !(self->flags & ITEM_SHARED))
    {
	vim_free(self->text);
	self->text = NULL;
    }
    END_DESTROY(PopupItem);
}

    static int
_PopupItem_cmp_score(a, b) /* PopupItemCompare_Fn */
    PopupItem_T* a;
    PopupItem_T* b;
{
    if (a->filter_score < b->filter_score) return LT_SORT_DOWN;
    if (a->filter_score > b->filter_score) return GT_SORT_DOWN;
    return 0;
}

/* [ooc]
 *
  class ItemProvider(object) [iprov]
  {
    array(PopupItem)	items;
    dict_T*		commands;  // commands defined in vim-script
    char_u*		title;
    void	init();
    void	destroy();
    void	read_options(dict_T* options);
    void	clear_items();
    int		get_item_count();
    PopupItem_T* get_item(int item);
    PopupItem_T* append_pchar_item(char_u* text, int shared); // shared=0 => will be free()-d
    char_u*	get_display_text(int item);
    // TODO: filternig can be implemented with a separate string or with a
    // pointer into the original string and the size of the substring. We could
    // assume that the part of the string to be filtered is contiguous.
    char_u*	get_filter_text(int item);
    char_u*	get_path_text();
    char_u*	get_title();
    uint	is_marked(int item);
    void	set_marked(int item, int marked);
    int		select_item(int item);
    int		select_parent();
    int		sort_items(PopupItemCompare_Fn cmp); // returns TRUE if qsort was called

    // the provider handles the command and returns the next command
    char_u*	handle_command(PopupList* puls, char_u* command, dict_T* status);

    // the provider can provide default keymaps for its commands
    void	default_keymap(PopupList* puls);
  };

*/

    static void
_iprov_init(_self)
    void* _self;
{
    METHOD(ItemProvider, init);
    self->commands = NULL;
    self->title = NULL;
}

    static void
_iprov_destroy(_self)
    void* _self;
{
    int i;
    METHOD(ItemProvider, destroy);
    /* delete cached text from items */
    self->op->clear_items(self);

    self->commands = NULL; /* we don't own them */
    _str_free(&self->title);

    END_DESTROY(ItemProvider);
}

    static void
_iprov_read_options(_self, options)
    void* _self;
    dict_T* options;
{
    METHOD(ItemProvider, read_options);
    dictitem_T* option;
    option = dict_find(options, "title", -1L);
    if (option && option->di_tv.v_type == VAR_STRING && option->di_tv.vval.v_string)
    {
	_str_assign(&self->title, option->di_tv.vval.v_string);
    }
}

    static void
_iprov_clear_items(_self)
    void* _self;
{
    int i;
    METHOD(ItemProvider, clear_items);
    /* delete cached text from items */
    for (i = 0; i < self->op->get_item_count(self); i++)
    {
	PopupItem_T* pit = self->op->get_item(self, i);
	if (pit)
	    _ppit_destroy(pit);
    }
    self->items.ga_len = 0;
}

    static int
_iprov_get_item_count(_self)
    void* _self;
{
    METHOD(ItemProvider, get_item_count);

    return self->items.ga_len;
}

    static PopupItem_T*
_iprov_get_item(_self, item)
    void* _self;
    int item;
{
    METHOD(ItemProvider, get_item);
    if (item < 0 || item >= self->items.ga_len)
	return NULL;

    return (PopupItem_T*) (self->items.ga_data + self->items.ga_itemsize * item);
}

    static PopupItem_T*
_iprov_append_pchar_item(_self, text, shared)
    void* _self;
    char_u* text;
    int shared;
{
    METHOD(ItemProvider, append_pchar_item);
    if (ga_grow(&self->items, 1) == OK)
    {
	self->items.ga_len++;
	PopupItem_T* pitnew = self->op->get_item(self, self->items.ga_len-1);
	_ppit_init(pitnew);
	pitnew->text = text;
	if (shared)
	    pitnew->flags |= ITEM_SHARED;

	return pitnew;
    }
    return NULL;
}

    static char_u*
_iprov_get_display_text(_self, item)
    void* _self;
    int item;
{
    METHOD(ItemProvider, get_display_text);
    return self->op->get_item(self, item)->text;
}

    static char_u*
_iprov_get_filter_text(_self, item)
    void* _self;
    int item;
{
    METHOD(ItemProvider, get_filter_text);
    PopupItem_T* pit = self->op->get_item(self, item);
    if (!pit)
	return NULL;
    return pit->text + pit->filter_start;
}

    static char_u*
_iprov_get_path_text(_self)
    void* _self;
{
    METHOD(ItemProvider, get_path_text);
    return NULL;
}

    static char_u*
_iprov_get_title(_self)
    void* _self;
{
    METHOD(ItemProvider, get_title);
    return self->title;
}

    static void
_iprov_set_marked(_self, item, marked)
    void* _self;
    int item;
    int marked;
{
    METHOD(ItemProvider, set_marked);

    PopupItem_T* pit = self->op->get_item(self, item);
    if (marked) pit->flags |= ITEM_MARKED;
    else pit->flags &= ~ITEM_MARKED;
}

    static uint
_iprov_is_marked(_self, item)
    void* _self;
    int item;
{
    METHOD(ItemProvider, is_marked);
    return self->op->get_item(self, item)->flags & ITEM_MARKED;
}

    static int
_iprov_select_item(_self, item)
    void* _self;
    int item;
{
    METHOD(ItemProvider, select_item);
    return 0; /* a leaf item, execute */
}

    static int
_iprov_select_parent(_self)
    void* _self;
{
    METHOD(ItemProvider, select_parent);
    return 0; /* no parent, ignore */
}

    static char_u*
_iprov_handle_command(_self, puls, command, status)
    void*	    _self;
    PopupList_T*    puls;
    char_u*	    command;
    dict_T*	    status;   /* the status of the popup list: selected item, marked items, etc. */
{
    METHOD(ItemProvider, handle_command);
    dictitem_T* icmd;

    if (!self->commands || !command)
	return NULL;

    icmd = dict_find(self->commands, command, -1L);
    if (!icmd)
	return NULL;

    {
	typval_T	argv[2 + 1]; /* command, status */
	typval_T	rettv;
	int		argc = 0;
	listitem_T	*item;
	int		dummy;
	dict_T	*selfdict = NULL;
	char_u* fn = icmd->di_tv.vval.v_string;

	argv[0].v_type = VAR_STRING;
	argv[0].vval.v_string = command;
	++argc;

	++status->dv_refcount;
	argv[1].v_type = VAR_DICT;
	argv[1].vval.v_dict = status;
	++argc;

	/* TODO: make call_func non-static in eval.c */
	call_func(fn, (int)STRLEN(fn), &rettv, argc, argv,
		curwin->w_cursor.lnum, curwin->w_cursor.lnum,
		&dummy, TRUE, selfdict);

	dict_unref(status); /* could also use clear_tv(&argv[1]) */
    }

    return NULL;
}

    static void
_iprov_default_keymap(_self, puls)
    void* _self;
    PopupList_T* puls;
{
    METHOD(ItemProvider, default_keymap);
}

    static int
_iprov_sort_items(_self, cmp)
    void* _self;
    PopupItemCompare_Fn cmp;
{
    METHOD(ItemProvider, sort_items);
    int ni;

    ni = self->op->get_item_count(self);
    if (ni < 2)
	return 0;

    qsort(self->op->get_item(self, 0), ni, sizeof(PopupItem_T), (__puls_compar_fn_t)cmp);
    return 1;
}

/* [ooc]
 *
  class VimlistItemProvider(ItemProvider) [vlprov]
  {
    list_T*	vimlist;
    int		_refcount; // how many times have we referenced the list
    void init();
    void set_list(list_T* vimlist);
    void destroy();
  };
*/

    static void
_vlprov_init(_self)
    void* _self;
{
    METHOD(VimlistItemProvider, init);

    self->vimlist = NULL;
    self->_refcount = 0;
}

/*
 * VimlistItemProvider.set_list(vimlist)
 * Use the vimlist as the source for popuplist items.
 * The current popuplist items are removed and replaced with the items from vimlist.
 * Only the items of type VAR_STRING are used.
 */
    static void
_vlprov_set_list(_self, vimlist)
    void* _self;
    list_T* vimlist;
{
    METHOD(VimlistItemProvider, set_list);

    if (self->vimlist != vimlist && self->vimlist && self->_refcount > 0)
    {
	list_unref(self->vimlist);
	self->_refcount = 0;
	self->vimlist = NULL;
    }

    self->op->clear_items(self);

    self->vimlist = vimlist;
    if (vimlist)
    {
	++vimlist->lv_refcount <= 0;
	self->_refcount = 1;

	listitem_T *pitem = vimlist->lv_first;
	while (pitem != NULL)
	{
	    if (pitem->li_tv.v_type == VAR_STRING && pitem->li_tv.vval.v_string)
	    {
		/* We assume the list will remain unchanged, and we share the value. */
		self->op->append_pchar_item(self, pitem->li_tv.vval.v_string, ITEM_SHARED);
	    }
	    else
		self->op->append_pchar_item(self, blankline, ITEM_SHARED);
	    pitem = pitem->li_next;
	}
    }
}

    static void
_vlprov_destroy(_self)
    void* _self;
{
    METHOD(VimlistItemProvider, destroy);
    if (self->vimlist && self->_refcount > 0)
    {
	/* TODO: self.vimlist.refcount--; */
	self->_refcount = 0;
	self->vimlist = NULL;
    }
    END_DESTROY(VimlistItemProvider);
}


#ifdef FEAT_POPUPLIST_BUFFERS
#include "puls_pb.c"
#endif


/* TODO: filtered items:
 *	- store sorted ranges of visible items
 *	- store count of ranges
 *	- store count of items
 * !!!! Not good! The filtered items are also sorted! 
 * XXX: do we want to have marked items that are currently hidden by the filter?
 *
 * Operation:
 *    - filter items into index field; a filter gives each item a score
 *    - sort the items by score, use stable sorting so that the item order is preserved within same score
 *    - XXX: what do we do with titles when sorting? User option while puls is visible:
 *	- keep the titles, sort items below titles
 *	- hide titles, sort all items
 */

/* [ooc]
 *
  const MAX_FILTER_SIZE = 128;
  class ItemFilter(object) [iflt]
  {
    ItemProvider* model;
    char text[MAX_FILTER_SIZE];
    array(int) items;  // indices of items in the model
    void init();
    void destroy();
    int  add_text(char_u* ptext);
    int  backspace();
    void filter_items();
    int  match(char_u* needle, char_u* haystack);
    int  get_item_count();
    int  get_model_index(int index);
  };
*/

    static void
_iflt_init(_self)
    void* _self;
{
    METHOD(ItemFilter, init);
    self->model = NULL;
    self->text[0] = NUL;
}

    static void
_iflt_destroy(_self)
    void* _self;
{
    METHOD(ItemFilter, destroy);
    self->model = NULL; /* filter doesn't own the model */
    END_DESTROY(ItemFilter);
}

    static int
_iflt_add_text(_self, ptext)
    void* _self;
    char_u* ptext;
{
    METHOD(ItemFilter, add_text);
    if (STRLEN(ptext) + STRLEN(self->text) >= MAX_FILTER_SIZE)
	return 0;

    strcat(self->text, ptext);
    return 1;
}

    static int
_iflt_backspace(_self)
    void* _self;
{
    METHOD(ItemFilter, backspace);
    char_u* last_char;
    if (STRLEN(self->text) < 1)
	return 0;

    last_char = self->text + STRLEN(self->text);
    mb_ptr_back(self->text, last_char);
    *last_char = NUL;
    return 1;
}

    static int
_iflt_match(_self, needle, haystack)
    void* _self;
    char_u* needle;
    char_u* haystack;
{
    METHOD(ItemFilter, match);
    char *p;
    if (! needle || ! *needle)
	return 1;
    p = _stristr(haystack, needle);
    if (! p)
	return 0;
    if (p == (char*)haystack) /* start of text */
	return 100;
    if (isalnum(*(p-1)) != isalnum(*needle)) /* simplistic start-of-word check */
	return 10;
    return 1;
}

    static void
_iflt_filter_items(_self)
    void* _self;
{
    METHOD(ItemFilter, filter_items);

    PopupItem_T* pit;
    int item_count = self->model->op->get_item_count(self->model);
    int i;

    if (STRLEN(self->text) < 1)
    {
	self->items.ga_len = 0;
	return;
    }

    self->items.ga_len = 0;
    for(i = 0; i < item_count; i++)
    {
	int score = self->op->match(self, self->text, self->model->op->get_filter_text(self->model, i));
	pit = self->model->op->get_item(self->model, i); /* TODO: set-item-score() */
	if (pit)
	    pit->filter_score = score;
	if (score <= 0)
	    continue;

	if (ga_grow(&self->items, 1) == OK)
	{
	    *(int*) (self->items.ga_data + self->items.ga_itemsize * self->items.ga_len) = i;
	    self->items.ga_len++;
	}
    }
#if 0
    printf("items %d, filtered %d\n\r", item_count, self->items.ga_len);
    if (model->op->has_titles())
	self->op->remove_orphan_titles(self);
#endif
    // TODO: Have to sort filtered items, not the actual items !!!!
    //  New qsort? Until then ... bubble sort.
    {
	PopupItem_T* pb;
	int j, tmp;
	int* data = (int*) self->items.ga_data;
	int len = self->items.ga_len;
	int changed = 1;
	while (changed)
	{
	    changed = 0;
	    for (i = 0; i < len - 1; i++)
	    {
		for (j = i + 1; j < len; j++)
		{
		    pit = self->model->op->get_item(self->model, data[i]);
		    pb = self->model->op->get_item(self->model, data[j]);
		    if (_PopupItem_cmp_score(pit, pb) > 0)
		    {
			tmp = data[i];
			data[i] = data[j];
			data[j] = tmp;
			changed = 1;
		    }
		}
	    }
	}
    }
}

    static int
_iflt_get_item_count(_self)
    void* _self;
{
    METHOD(ItemFilter, get_item_count);
    if (STRLEN(self->text) < 1)
	return self->model->op->get_item_count(self->model);

    return self->items.ga_len;
}

    static int
_iflt_get_model_index(_self, index)
    void* _self;
    int index;
{
    METHOD(ItemFilter, get_model_index);
    if (STRLEN(self->text) < 1)
	return index;

    if (index < 0 || index >= self->items.ga_len)
	return -1;

    return *(int*) (self->items.ga_data + self->items.ga_itemsize * index);
}

/* [ooc]
 *
  struct Box [box] {
    int left;
    int top;
    int width;
    int height;
    void init();
    int right();   // right column
    int bottom();  // bottom row
    void move(int x, int y);
    void resize(int width, int height);
  };
*/

    static void
_box_init(_self)
    void* _self;
{
    METHOD(Box, init);
    self->left = 0;
    self->top = 0;
    self->width = 0;
    self->height = 0;
}

    static int
_box_right(_self)
    void* _self;
{
    METHOD(Box, right);
    return self->left + self->width - 1;
}

    static int
_box_bottom(_self)
    void* _self;
{
    METHOD(Box, bottom);
    return self->top + self->height - 1;
}

    static void
_box_move(_self, x, y)
    void* _self;
    int x;
    int y;
{
    METHOD(Box, move);
    self->left = x;
    self->top = y;
}

    static void
_box_resize(_self, width, height)
    void* _self;
    int width;
    int height;
{
    METHOD(Box, resize);
    self->width = width;
    self->height = height;
}


/* [ooc]
 *
  class BoxAligner [bxal] {
    Box	    limits;
    char    popup[2];	// part of popup-box to align; 012; a=auto(larger space)
    char    screen[2];	// screen point to align to; 0123 4 5678; c=cursor; C=below/above cursor; xy
    int	    screen_x;	// coordinate for screen[0]=='x'
    int	    screen_y;	// coordinate for screen[1]=='y'

    void    init();
    void    destroy();
    void    set_limits(int left, int top, int right, int bottom);
    void    parse_screen_pos(char_u* posdef);
    void    set_align_params(dict_T* params);
    void    align(Box* box, WindowBorder* border);
  };
*/

    static void
_bxal_init(_self)
    void*   _self;
{
    METHOD(BoxAligner, init);

    _box_init(&self->limits);
    self->op->set_limits(self, 0, 0, Columns-1, Rows-1);

    /* default alignment: centered on screen */
    self->popup[0] = 's';
    self->popup[1] = 's';
    self->screen[0] = '4';
    self->screen[1] = '4';
}

    static void
_bxal_destroy(_self)
    void*   _self;
{
    METHOD(BoxAligner, destroy);
    END_DESTROY(BoxAligner);
}

    static void
_bxal_set_limits(_self, left, top, right, bottom)
    void*   _self;
    int	    left;
    int	    top;
    int	    right;
    int	    bottom;
{
    METHOD(BoxAligner, set_limits);
    self->limits.left = left;
    self->limits.top = top;
    self->limits.width = right-left+1;
    self->limits.height = bottom-top+1;
}

    static void
_bxal_parse_screen_pos(_self, posdef)
    void*	_self;
    char_u*	posdef;
{
    METHOD(BoxAligner, parse_screen_pos);
    int i;
    if (! posdef)
	return;

    for (i = 0; i < 2; i++)
    {
	if (*posdef >= '0' && *posdef <= '8')
	{
	    self->screen[i] = *posdef;
	}
	/* TODO: screen position: c, x, y */
	if (*posdef == NUL)
	    break;
	++posdef;
    }
}

    static void
_bxal_set_align_params(_self, params)
    void*	_self;
    dict_T*	params;
{
    METHOD(BoxAligner, set_align_params);
}

    static int
_num_to_coord(num, cmin, cmax)
    char    num;
    int	    cmin;
    int	    cmax;
{
    if (num < '0' || num > '8' || cmax < cmin)
	return -1;

    return (int) (cmin + (num - '0') * (cmax - cmin) / 8.0f + 0.5f);
}

    static void
_bxal_align(_self, box, border)
    void*		_self;
    Box_T*		box;
    WindowBorder_T*	border;
{
    METHOD(BoxAligner, align);
    int px, py, sx, sy;
    Box_T* limits = &self->limits;

    LOG(("Screen: rows=%d cols=%d", Rows, Columns));
    LOG(("Limits: l=%d, t=%d, r=%d, b=%d", limits->left, limits->top, _box_right(limits), _box_bottom(limits)));
    LOG(("Pos: on_screen=%c%c box_point=%c%c", self->screen[0], self->screen[1], self->popup[0], self->popup[1]));

    sx = _num_to_coord(self->screen[0], 0, limits->width-1);
    sy = _num_to_coord(self->screen[1], 0, limits->height-1);
    if (self->popup[0] == 's')
	px = (int) (box->width * (1.0 * sx / limits->width) + 0.5);
    else
	px = _num_to_coord(self->popup[0], 0, box->width-1);
    if (self->popup[1] == 's')
	py = (int) (box->height * (1.0 * sy / limits->height) + 0.5);
    else
	py = _num_to_coord(self->popup[1], 0, box->height-1);

    box->left = limit_value(limits->left + sx - px, limits->left, _box_right(limits));
    box->top = limit_value(limits->top + sy - py, limits->top, _box_bottom(limits));

    LOG(("Box A: l=%d, t=%d, r=%d, b=%d", box->left, box->top, _box_right(box), _box_bottom(box)));

    if (border)
    {
	/* adjust if the border is present and outside of limits */
	if (border->active[WINBORDER_LEFT] && box->left - 1 < limits->left)
	    ++box->left;
	else if (border->active[WINBORDER_RIGHT] && _box_right(box) + 1 > _box_right(limits))
	    --box->left;
	if (border->active[WINBORDER_TOP] && box->top - 1 < limits->top)
	    ++box->top;
	else if (border->active[WINBORDER_BOTTOM] && _box_bottom(box) + 1 > _box_bottom(limits))
	    --box->top;

	LOG(("Box B: l=%d, t=%d, r=%d, b=%d", box->left, box->top, _box_right(box), _box_bottom(box)));
    }
}

/* [ooc]
 *
  enum FindKeyResult { KM_NOTFOUND, KM_PREFIX, KM_MATCH };
  const KM_NOTFOUND = 0;
  const KM_PREFIX   = 1;
  const KM_MATCH    = 2;
  class SimpleKeymap [skmap]
  {
     char_u*	name;
     dict_T*	key2cmd;    // maps a raw Vim sequence to a command name
     // Vim is able to produce a nice name from a given raw sequence ...
     //    - eval_vars() expands <cword> etc. but not keys
     //    - get_string_tv() (eval.c) -> trans_special() (misc2.c) -> find_special_key() translates <keys>
     //    - msg_outtrans_special() (message.c) -> str2special() translates raw bytes to <keys>

     void   init();
     void   destroy();
     void   set_name(char_u* name);

     // Encode a Vim <key> sequence to a raw vim key sequence
     char_u*	encode_key(char_u* sequence);
     void	set_vim_key(char_u* sequence, char_u* command);

     // The following commands work with raw sequences.
     void	set_key(char_u* sequence, char_u* command);
     char_u*	get_command(char_u* sequence, int copy);
     int	find_key(char_u* sequence);
     // TODO later:
     // void	clear_key(char_u* sequence);
     // void	clear_key_prefix(char_u* sequence);
     void	clear_all_keys();
     // void	get_mapped_keys(); // needed only to implement help
  };
*/

    static void
_skmap_init(_self)
    void* _self;
{
    METHOD(SimpleKeymap, init);
    self->name = NULL;
    self->key2cmd = dict_alloc();
    ++self->key2cmd->dv_refcount;
}

    static void
_skmap_destroy(_self)
    void* _self;
{
    METHOD(SimpleKeymap, destroy);
    _str_free(&self->name);
    if (self->key2cmd)
    {
	dict_unref(self->key2cmd); /* TODO: make dict_unref non-static in eval.c */
	self->key2cmd = NULL;
    }
    END_DESTROY(SimpleKeymap);
}

    static void
_skmap_set_name(_self, name)
    void* _self;
    char_u* name;
{
    METHOD(SimpleKeymap, set_name);
    _str_assign(&self->name, name);
};

    static char_u*
_skmap_encode_key(_self, sequence)
    void* _self;
    char_u* sequence;
{
    METHOD(SimpleKeymap, encode_key);
    char_u seq[128];
    char_u* raw;

    /* enclose in quotes to create a vim string */
    raw = vim_strsave_escaped(sequence, "\"<");
    seq[0] = '"';
    STRNCPY(&seq[1], raw, 126);
    strcat(seq, "\"");
    vim_free(raw);

    /* evaluate the vim string */
    raw = eval_to_string_safe(seq, NULL, TRUE);
    /* XXX: diag: printf("seq: '%s', str: '%s', raw: '%s'\n\r", sequence, seq, raw);*/
    return raw;
}

    static void
_skmap_set_vim_key(_self, sequence, command)
    void* _self;
    char_u* sequence;
    char_u* command;
{
    METHOD(SimpleKeymap, set_vim_key);
    sequence = self->op->encode_key(self, sequence);
    self->op->set_key(self, sequence, command);
}

    static void
_skmap_set_key(_self, sequence, command)
    void* _self;
    char_u* sequence;
    char_u* command;
{
    METHOD(SimpleKeymap, set_key);
    if (!sequence || !command)
	return;

    /* TODO: Check for conflicting sequences */
    dict_add_nr_str(self->key2cmd, sequence, 0, command);
}

    static char_u*
_skmap_get_command(_self, sequence, copy)
    void* _self;
    char_u* sequence;
    int copy;
{
    METHOD(SimpleKeymap, get_command);
    dictitem_T* seqmap;
    seqmap = dict_find(self->key2cmd, sequence, -1L);
    if (seqmap)
    {
	if (copy)
	    return vim_strsave(seqmap->di_tv.vval.v_string);
	else
	    return seqmap->di_tv.vval.v_string;
    }

    return NULL;
}

    static int
_skmap_find_key(_self, sequence)
    void* _self;
    char_u* sequence;
{
    METHOD(SimpleKeymap, find_key);
    hashitem_T	*hi;
    dictitem_T* seqmap;
    DictIterator_T itkeys;
    int seq_len, is_prefix, todo;

    seqmap = dict_find(self->key2cmd, sequence, -1L);
    if (seqmap)
	return KM_MATCH;

    /* Test if sequence is a prefix of any of the items in the current modemap */
    is_prefix = 0;
    seq_len = STRLEN(sequence);

    _dicti_init(&itkeys);
    for(seqmap = itkeys.op->begin(&itkeys, self->key2cmd); seqmap != NULL; seqmap = itkeys.op->next(&itkeys))
    {
	if (EQUALSN(seqmap->di_key, sequence, seq_len))
	{
	    is_prefix = 1;
	    break;
	}
    }

    return is_prefix ? KM_PREFIX : KM_NOTFOUND;
}

    static void
_skmap_clear_all_keys(_self)
    void* _self;
{
    METHOD(SimpleKeymap, clear_all_keys);
    if (! self->key2cmd)
	return;
    if ((int)self->key2cmd->dv_hashtab.ht_used < 1)
	return;

    /* XXX: can't find suitable methods, so we delete and rectreate the dictionary */
    dict_unref(self->key2cmd);
    self->key2cmd = dict_alloc();
    ++self->key2cmd->dv_refcount;
}

/* Attribute intialization. Use custom names for the popup list.
 * If a name doesn't exist in the syntax table, use PUM values. */
typedef struct _puls_hl_attrs_T {
    char* name;		/* name used in syntax files */
    int   attr;		/* attribute returned by syn_id2attr */

    /* Positive: PUM HLF_xxx values to get default attr values.
     * Negative or zero: index into _puls_hl_attrs, a back-reference. */
    int   default_id;
} _puls_hl_attrs_T;

#define PULSATTR_NORMAL		0
#define PULSATTR_SELECTED	1
#define PULSATTR_TITLE		2
#define PULSATTR_TITLE_SEL	3
#define PULSATTR_MARKED		4
#define PULSATTR_MARKED_SEL	5
#define PULSATTR_BORDER		6
#define PULSATTR_SCROLL_BAR	7
#define PULSATTR_SCROLL_THUMB	8
#define PULSATTR_SCROLL_SPACE	9 /* special attr when scrollbar page is rendered with spaces */
#define PULSATTR_INPUT		10
#define PULSATTR_INPUT_ACTIVE	11

static _puls_hl_attrs_T _puls_hl_attrs[] = {
    { "PulsNormal",	    0, HLF_PNI },
    { "PulsSelected",	    0, HLF_PSI },
    { "PulsTitleItem",	    0, -PULSATTR_NORMAL },
    { "PulsTitleItemSel",   0, -PULSATTR_SELECTED },
    { "PulsMarked",	    0, HLF_PSI },
    { "PulsMarkedSel",	    0, -PULSATTR_SELECTED },
    { "PulsBorder",	    0, -PULSATTR_NORMAL },
    { "PulsScrollBar",	    0, -PULSATTR_BORDER },
    { "PulsScrollThumb",    0, HLF_PST },
    { "PulsScrollBarSpace", 0, HLF_PSB },
    { "PulsInput",	    0, -PULSATTR_BORDER },
    { "PulsInputActive",    0, HLF_PSI }
};

    static void
_update_hl_attrs()
{
    int i, size, id;
    size = sizeof(_puls_hl_attrs) / sizeof(_puls_hl_attrs[0]);
    for (i = 0; i < size; i++)
    {
	id = syn_name2id(_puls_hl_attrs[i].name);
	if (id > 0)
	    _puls_hl_attrs[i].attr = syn_id2attr(id);
	else
	{
	    id = _puls_hl_attrs[i].default_id;
	    if (id > 0)
		_puls_hl_attrs[i].attr = syn_id2attr(id);
	    else
		_puls_hl_attrs[i].attr = _puls_hl_attrs[-id].attr;
	}
    }
}


/* [ooc]
 *
  const WINBORDER_TOP = 0;
  const WINBORDER_RIGHT = 1;
  const WINBORDER_BOTTOM = 2;
  const WINBORDER_LEFT = 3;
  const SCROLLBAR_BAR = 0;
  const SCROLLBAR_THUMB = 1;
  class WindowBorder [wbor]
  {
    Box*    inner_box;
    int     item_count;
    int     active[4];		// visible sides: 'trbl'; 0 - off, otherwise on
    int     border_chars[8];	// spaces, single, double, minus/bar, ...
    int     scrollbar_chars[2];
    int     scrollbar_attr[2];
    int     scrollbar_thumb;	// size of the scrollbar thumb; 0 - SB is off
    int     scrollbar_pos;	// left or right border
    int     border_attr;
    char_u* title;
    char_u* mode; 	    // one or two characters
    char_u* info; 	    // arbitrary info, eg. position; right aligned in frame
    char_u* input;	    // current input; contents is right aligned in the field
    int     input_active;   // input field is active, special display

    void init();
    void destroy();
    void set_title(char_u* title);
    void set_mode_text(char_u* mode); 
    void set_input_text(char_u* text);
    void set_input_active(int active);
    void set_info(char_u* text);
    void prepare_scrollbar(int item_count);
    int  get_scrollbar_kind(int line, int current); // kind of char to draw in line; bar or thumb
    void draw_top();
    void draw_item_left(int line, int current);
    void draw_item_right(int line, int current);
    void draw_bottom();
  };

*/

// t, tr, r, br, b, bl, l, tl
static int _frame_blank[]  = { ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' };
static int _frame_ascii[]  = { '-', '+', '|', '+', '-', '+', '|', '+' };
#ifndef FEAT_MBYTE
static int _frame_single[]  = { '-', '+', '|', '+', '-', '+', '|', '+' };
static int _frame_double[]  = { '=', '+', '|', '+', '=', '+', '|', '+' };
static int _frame_mixed1[]  = { '-', '+', '|', '+', '-', '+', '|', '+' };
static int _frame_mixed2[]  = { '=', '+', '|', '+', '=', '+', '|', '+' };
static int _frame_custom[]  = { '-', '+', '|', '+', '-', '+', '|', '+' };
#else
/* utf-8: "─┐│┘─└│┌"  */
static int _frame_single[]  = { 0x2500, 0x2510, 0x2502, 0x2518, 0x2500, 0x2514, 0x2502, 0x250c };
/* utf-8: "═╗║╝═╚║╔" */
static int _frame_double[]  = { 0x2550, 0x2557, 0x2551, 0x255d, 0x2550, 0x255a, 0x2551, 0x2554 };
/* utf-8: "─╖║╜─╙║╓" */
static int _frame_mixed1[]  = { 0x2500, 0x2556, 0x2551, 0x255c, 0x2500, 0x2559, 0x2551, 0x2553 };
/* utf-8: "═╕│╛═╘│╒" */
static int _frame_mixed2[]  = { 0x2550, 0x2555, 0x2502, 0x255b, 0x2550, 0x2558, 0x2502, 0x2552 };
/* single again */
static int _frame_custom[]  = { 0x2500, 0x2510, 0x2502, 0x2518, 0x2500, 0x2514, 0x2502, 0x250c };
#endif
static int* _frames[] = {
    _frame_blank, _frame_ascii,
    _frame_single, _frame_double,
    _frame_mixed1, _frame_mixed2,
    _frame_custom
};


    static void
_wbor_init(_self)
    void* _self;
{
    METHOD(WindowBorder, init);
    int i;
    int *pframe;
    for (i = 0; i < 4; i++)
	self->active[i] = 1;
    self->scrollbar_thumb = 0;

    /* TODO: user option for frame type: 0-7; 0: no frame, 1: blank, 2: ascii, ...;
     * TODO: frames 3-7 can be changed with options */
#ifdef FEAT_MBYTE
    if (has_mbyte) pframe = _frames[3];
    else pframe = _frames[0];
#else
    pframe = _frames[0];
#endif
    for (i = 0; i < 8; i++)
	self->border_chars[i] = pframe[i];

    self->border_attr = _puls_hl_attrs[PULSATTR_BORDER].attr;

    // background, thumb
    self->scrollbar_chars[SCROLLBAR_BAR]   = self->border_chars[WINBORDER_RIGHT*2]; 
    self->scrollbar_chars[SCROLLBAR_THUMB] = ' '; 
    if (self->scrollbar_chars[SCROLLBAR_BAR] == ' ')
	self->scrollbar_attr[SCROLLBAR_BAR] = _puls_hl_attrs[PULSATTR_SCROLL_SPACE].attr;
    else
	self->scrollbar_attr[SCROLLBAR_BAR] = _puls_hl_attrs[PULSATTR_SCROLL_BAR].attr;
    self->scrollbar_attr[SCROLLBAR_THUMB]  = _puls_hl_attrs[PULSATTR_SCROLL_THUMB].attr;
    self->scrollbar_pos = WINBORDER_RIGHT;
    self->title = NULL;
    self->info = NULL;
    self->mode = NULL;
    self->input = NULL;
    self->input_active = 0;
}

    static void
_wbor_destroy(_self)
    void* _self;
{
    METHOD(WindowBorder, destroy);
    self->inner_box = NULL;     /* frame doesn't own the box */
    _str_free(&self->title);
    _str_free(&self->info);
    _str_free(&self->mode);
    _str_free(&self->input);
    END_DESTROY(WindowBorder);
}

    static void
_wbor_set_title(_self, title)
    void* _self;
    char_u* title;
{
    METHOD(WindowBorder, set_title);
    _str_assign(&self->title, title);
}

    static void
_wbor_set_mode_text(_self, mode)
    void* _self;
    char_u* mode;
{
    METHOD(WindowBorder, set_mode_text);
    char_u buf[16];
    char_u *p;
    int i = 2;
    p = buf;
    if (mode)
    {
	while (i > 0 && *mode != NUL)
	{
	    MB_COPY_CHAR(mode, p);
	    --i;
	}
    }
    while (i > 0)
    {
	int ch = self->border_chars[WINBORDER_BOTTOM*2];
	p += mb_char2bytes(ch, p);
	--i;
    }
    *p = NUL;

    _str_assign(&self->mode, buf);
}

    static void
_wbor_set_input_text(_self, text)
    void* _self;
    char_u* text;
{
    METHOD(WindowBorder, set_input_text);
    _str_assign(&self->input, text);
}

    static void
_wbor_set_input_active(_self, active)
    void* _self;
    int active;
{
    METHOD(WindowBorder, set_input_active);
    self->input_active = active;
}

    static void
_wbor_set_info(_self, text)
    void* _self;
    char_u* text;
{
    METHOD(WindowBorder, set_info);
    _str_assign(&self->info, text);
}

    static void
_wbor_prepare_scrollbar(_self, item_count)
    void* _self;
    int item_count;
{
    METHOD(WindowBorder, prepare_scrollbar);
    int h;
    self->item_count = item_count;
    if (! self->inner_box)
	return;
    h = self->inner_box->height;
    if (item_count <= h)
    {
	self->scrollbar_thumb = 0;
	return;
    }
    self->scrollbar_thumb = h * h / item_count;
    if (self->scrollbar_thumb < 1)
	self->scrollbar_thumb = 1;
}

    static int
_wbor_get_scrollbar_kind(_self, line, current)
    void* _self;
    int line;
    int current;
{
    METHOD(WindowBorder, get_scrollbar_kind);
    int sbtop = self->inner_box->height - self->scrollbar_thumb;
    sbtop = (int) ((float)sbtop * current / self->item_count + 0.5);
    if (line >= sbtop && line < sbtop + self->scrollbar_thumb)
	return SCROLLBAR_THUMB;
    return SCROLLBAR_BAR;
}

    static void
_wbor_draw_top(_self)
    void* _self;
{
    METHOD(WindowBorder, draw_top);
    int row, col, right, ch;
    LineWriter_T* writer;
    if (! self->inner_box || !self->active[WINBORDER_TOP])
	return;

    row = self->inner_box->top - 1;
    col = self->inner_box->left;
    right = _box_right(self->inner_box);
    if (self->active[WINBORDER_LEFT])
	screen_putchar(self->border_chars[WINBORDER_LEFT*2+1], row, col-1, self->border_attr);
    screen_putchar(self->border_chars[WINBORDER_TOP*2], row, col, self->border_attr);

    writer = new_LineWriter();
    writer->min_col = self->inner_box->left + 1;
    writer->max_col = right - 1;
    writer->tab_size = -1;
    ch = self->border_chars[WINBORDER_TOP*2];
    writer->op->write_line(writer, self->title, row, self->border_attr, ch);

    screen_putchar(self->border_chars[WINBORDER_TOP*2], row, right, self->border_attr);
    if (self->active[WINBORDER_RIGHT])
	screen_putchar(self->border_chars[WINBORDER_TOP*2+1], row, right+1, self->border_attr);

    CLASS_DELETE(writer);
}

    static void
_wbor_draw_bottom(_self)
    void* _self;
{
    METHOD(WindowBorder, draw_bottom);
    int row, col, endcol, right, ch, chbot, attr, iw;
    LineWriter_T* writer;
    if (! self->inner_box || !self->active[WINBORDER_BOTTOM])
	return;

    row = _box_bottom(self->inner_box) + 1;
    col = self->inner_box->left;
    right = _box_right(self->inner_box);
    attr = self->border_attr;
    LOG(("draw_bottom: row=%d col=%d", row, col));
    if (self->active[WINBORDER_LEFT])
    {
	screen_putchar(self->border_chars[WINBORDER_BOTTOM*2+1], row, col-1, attr);
    }

    chbot = self->border_chars[WINBORDER_BOTTOM*2];
    screen_putchar(chbot, row, col, attr);
    ++col;

    writer = new_LineWriter();
    writer->tab_size = -1;

    /* MODE */
    writer->op->set_limits(writer, col, col+1);
    writer->op->write_line(writer, self->mode, row, attr, chbot);
    col += 2;

    screen_putchar(chbot, row, col, attr);
    ++col;

    /* INPUT */
    /* TODO: change attr and fillChar accordning on input_active */
    if (self->input_active)
    {
	attr = _puls_hl_attrs[PULSATTR_INPUT_ACTIVE].attr;
	ch = ' ';
    }
    else
    {
	attr = _puls_hl_attrs[PULSATTR_INPUT].attr;
	ch = chbot;
    }
    iw = self->inner_box->width / 3;
    if (iw < 8)
	iw = 8;
    endcol = col + iw;
    if (endcol > right)
	endcol = right;
    writer->op->set_limits(writer, col, endcol);
    writer->op->write_line(writer, self->input, row, attr, ch);
    col = endcol + 1;

    attr = self->border_attr;
    if (col < right)
    {
	screen_putchar(chbot, row, col, attr);
	++col;
    }

    /* INFO */
    if (col < right)
    {
	writer->op->set_limits(writer, col, right - 1);
	writer->op->write_line(writer, self->info, row, attr, chbot);
    }

    screen_putchar(chbot, row, right, attr);

    if (self->active[WINBORDER_RIGHT])
    {
	screen_putchar(self->border_chars[WINBORDER_BOTTOM*2-1], row, right+1, attr);
    }

    CLASS_DELETE(writer);
}

    static void
_wbor_draw_item_left(_self, line, current)
    void* _self;
    int line;
    int current;
{
    METHOD(WindowBorder, draw_item_left);
    int row, col;
    if (! self->inner_box)
	return;
    if (! self->active[WINBORDER_LEFT])
	return;

    col = self->inner_box->left - 1;
    if (col < 0)
	return;
    row = self->inner_box->top + line;

    if (self->scrollbar_pos == WINBORDER_LEFT && self->scrollbar_thumb > 0)
    {
	int ci = self->op->get_scrollbar_kind(self, line, current);
	screen_putchar(self->scrollbar_chars[ci], row, col, self->scrollbar_attr[ci]);
	return;
    }

    screen_putchar(self->border_chars[WINBORDER_RIGHT*2], row, col, self->border_attr);
}

    static void
_wbor_draw_item_right(_self, line, current)
    void* _self;
    int line;
    int current;
{
    METHOD(WindowBorder, draw_item_right);
    int row, col;
    if (! self->inner_box)
	return;
    if (! self->active[WINBORDER_RIGHT])
	return;

    col = _box_right(self->inner_box) + 1;
    if (col > Columns)
	return;
    row = self->inner_box->top + line;

    if (self->scrollbar_pos == WINBORDER_RIGHT && self->scrollbar_thumb > 0)
    {
	int ci = self->op->get_scrollbar_kind(self, line, current);
	screen_putchar(self->scrollbar_chars[ci], row, col, self->scrollbar_attr[ci]);
	return;
    }

    screen_putchar(self->border_chars[WINBORDER_RIGHT*2], row, col, self->border_attr);
}

/* [ooc]
 *
  const PULS_DEF_HEIGHT = 10;
  const PULS_DEF_WIDTH  = 20;
  const PULS_MIN_WIDTH  = 10;
  const PULS_MIN_HEIGHT = 1;
  const PULS_REDRAW_CURRENT = 0x01;
  const PULS_REDRAW_FRAME   = 0x02;
  const PULS_REDRAW_ALL     = 0x80;
  class PopupList(object) [puls]
  {
    ItemProvider*   model;	// items of the displayed puls
    ItemFilter*	    filter;	// selects a subset of model->items
    SimpleKeymap*   km_normal;
    SimpleKeymap*   km_filter;
    BoxAligner*	    aligner;	// positions the list box on the screen
    WindowBorder*   border;
    Box position;	    // position of the items, without the frame
    int current;	    // index of selected item or -1
    int first;		    // index of top item
    int leftcolumn;	    // first displayed column
    int column_split;	    // TRUE when displayed in two columns split by a tab
    int col0_width;	    // width of column 0
    int col1_width;	    // width of column 1; only valid when column_split is TRUE
    int split_width;	    // the displayed width of column 0; less or eqal to col0_width
    int need_redraw;	    // redraw is needed 
    char_u* title;

    void    init();
    void    destroy();
    void    default_keymap();
    void    map_keys(char_u* kmap_name, dict_T* kmap);
    int	    calc_size(int limit_width, int limit_height);
    void    reposition();
    void    redraw();
    int	    do_command(char_u* command);
    void    prepare_result(dict_T* result);
    void    set_title(char_u* title);
    void    set_current(int index);
  };

*/

    static void
_puls_init(_self)
    void* _self;
{
    METHOD(PopupList, init);
    self->model = NULL;
    self->filter = NULL;
    self->aligner = NULL;
    self->title = NULL;
    self->first = 0;
    self->current = 0;
    self->leftcolumn = 0;
    self->column_split = 0;
    self->col0_width = 0;
    self->col1_width = 0;
    self->need_redraw = 0;
    self->km_normal = new_SimpleKeymap();
    self->km_normal->op->set_name(self->km_normal, "normal");
    self->km_filter = new_SimpleKeymap();
    self->km_filter->op->set_name(self->km_filter, "filter");
    _box_init(&self->position);
    self->border = new_WindowBorder();
    self->border->inner_box = &self->position;

    self->op->default_keymap(self);
}

    static void
_puls_destroy(_self)
    void* _self;
{
    METHOD(PopupList, destroy);
    if (self->model)
	self->model = NULL;	/* puls doesn't own the model */
    if (self->filter)
	self->filter = NULL;	/* puls doesn't own the filter */
    if (self->aligner)
	self->aligner = NULL;	/* puls doesn't own the aligner */

    CLASS_DELETE(self->km_normal);
    CLASS_DELETE(self->km_filter);
    CLASS_DELETE(self->border);
    _str_free(&self->title);

    END_DESTROY(PopupList);
}


    static void
_puls_set_title(_self, title)
    void* _self;
    char_u* title;
{
    METHOD(PopupList, set_title);
    _str_assign(&self->title, title);
    self->border->op->set_title(self->border, title);
}


    static void
_puls_default_keymap(_self)
    void* _self;
{
    METHOD(PopupList, default_keymap);
    SimpleKeymap_T* modemap;
    modemap = self->km_normal;
    modemap->op->clear_all_keys(modemap);
    modemap->op->set_key(modemap, "q", "quit");
    modemap->op->set_key(modemap, "j", "next-item");
    modemap->op->set_key(modemap, "k", "prev-item");
    modemap->op->set_key(modemap, "n", "next-page");
    modemap->op->set_key(modemap, "p", "prev-page");
    modemap->op->set_key(modemap, "h", "shift-left");
    modemap->op->set_key(modemap, "l", "shift-right");
    modemap->op->set_key(modemap, "m", "toggle-marked");
    modemap->op->set_key(modemap, "f", "modeswitch:filter");
    modemap->op->set_key(modemap, "theauthor", "markomahnic");
    modemap->op->set_vim_key(modemap, "<cr>", "accept");
    modemap->op->set_vim_key(modemap, "<esc>", "quit");

    modemap = self->km_filter;
    modemap->op->clear_all_keys(modemap);
    modemap->op->set_vim_key(modemap, "<cr>", "modeswitch:normal");
    modemap->op->set_vim_key(modemap, "<esc>", "quit");
    modemap->op->set_vim_key(modemap, "<backspace>", "filter-bs");
}

    static void
_puls_map_keys(_self, kmap_name, kmap)
    void* _self;
    char_u* kmap_name;
    dict_T* kmap;
{
    METHOD(PopupList, map_keys);
    SimpleKeymap_T* modemap = NULL;
    dictitem_T* pkd;
    DictIterator_T itkey;

    if (!kmap_name || !kmap)
	return;
    if (self->km_normal && EQUALS(kmap_name, self->km_normal->name))
	modemap = self->km_normal;
    else if (self->km_filter && EQUALS(kmap_name, self->km_filter->name))
	modemap = self->km_filter;
    else
	return; /* TODO: WARN - invalid keymap name! */

    _dicti_init(&itkey);
    for (pkd = itkey.op->begin(&itkey, kmap); pkd != NULL; pkd = itkey.op->next(&itkey))
    {
	if (pkd->di_tv.v_type == VAR_STRING)
	{
	    modemap->op->set_key(modemap, pkd->di_key, pkd->di_tv.vval.v_string);
	}
    }
}

    static void
_puls_prepare_result(_self, result)
    void* _self;
    dict_T* result;
{
    METHOD(PopupList, prepare_result);
    list_T* marked;
    int idx;
    if (! result)
	return;
    idx = self->filter->op->get_model_index(self->filter, self->current);
    dict_add_nr_str(result, "current", idx, NULL);
    marked = list_alloc();
    /* TODO: fill the marked list */
    dict_add_list(result, "marked", marked);
    /* TODO: fill the path for hierarchical list */
}

    static int
_puls_calc_size(_self, limit_width, limit_height)
    void*	_self;
    int		limit_width;
    int		limit_height;
{
    int i;
    int w, max_width, max_width_1;
    char_u *text;
    char_u *start;
    char_u *pos;
    int item_count;

    METHOD(PopupList, calc_size);

    limit_width =  limit_value(limit_width, PULS_MIN_WIDTH, Columns-2);
    limit_height = limit_value(limit_height, 1, Rows-2);
    item_count = self->model->op->get_item_count(self->model);

    /* TODO: the PULS could be configured for autosize. In this case we would measure
     * the size of filtered items instead of all items.
     */

    /* Compute the width of the widest item. */
    if ( ! self->column_split) 
    {
	max_width = 0;
	for (i = 0; i < item_count; ++i)
	{
	    text = self->model->op->get_display_text(self->model, i);
	    w = vim_strsize(text);
	    if (max_width < w)
		max_width = w;
	}
	self->col0_width = max_width;
	self->col1_width = 0;
    }
    else
    {
	max_width = 0;
	max_width_1 = 0;
	for (i = 0; i < item_count; ++i)
	{
	    start = self->model->op->get_display_text(self->model, i);
	    pos = vim_strchr(start, '\t');
	    if (pos)
	    {
		w = vim_strnsize(start, pos-start);
		if (max_width < w)
		    max_width = w;
		w = vim_strsize(pos+1);
		if (max_width_1 < w)
		    max_width_1 = w;
	    }
	    else
	    {
		w = vim_strsize(start);
		if (max_width < w)
		    max_width = w;
	    }
	}
	self->col0_width = max_width;
	self->col1_width = max_width_1;
    }

    /* calculate the size within limits */
    self->position.height = limit_value(item_count, 1, limit_height);

    if (! self->column_split)
    {
	self->position.width = limit_value(max_width, PULS_MIN_WIDTH, limit_width);
	self->split_width = 0;
    }
    else
    {
	self->position.width = limit_value(max_width + max_width_1 + 2, PULS_MIN_WIDTH, limit_width+1);
	self->split_width = max_width + 2;
	if (self->position.width > limit_width)
	{
	    self->position.width = limit_width;
	    w = (int) (limit_width * 0.4); /* TODO: option first_column_size_max=0.4 */
	    w = limit_value(w, PULS_MIN_WIDTH, max_width + 2);
	    self->split_width = w;
	}
    }

    /* accomodate space for the (outer) border */
    if (self->border)
    {
	/* WIDTH */
	w = self->position.width;
	if (self->border->active[WINBORDER_LEFT])
	    ++w;
	if (self->border->active[WINBORDER_RIGHT])
	    ++w;
	while(w > limit_width && w > PULS_MIN_WIDTH)
	{
	    --self->position.width;
	    --w;
	}
	while(w > limit_width && self->border->active[WINBORDER_LEFT])
	{
	    self->border->active[WINBORDER_LEFT] = 0;
	    --w;
	}
	while(w > limit_width && self->border->active[WINBORDER_RIGHT])
	{
	    self->border->active[WINBORDER_RIGHT] = 0;
	    --w;
	}

	/* HEIGHT */
	w = self->position.height;
	if (self->border->active[WINBORDER_TOP])
	    ++w;
	if (self->border->active[WINBORDER_BOTTOM])
	    ++w;
	while(w > limit_height && w > PULS_MIN_HEIGHT)
	{
	    --self->position.height;
	    --w;
	}
	while(w > limit_height && self->border->active[WINBORDER_TOP])
	{
	    self->border->active[WINBORDER_TOP] = 0;
	    --w;
	}
	while(w > limit_height && self->border->active[WINBORDER_BOTTOM])
	{
	    self->border->active[WINBORDER_BOTTOM] = 0;
	    --w;
	}
    }

    return self->position.width >= PULS_MIN_WIDTH && self->position.height >= PULS_MIN_HEIGHT;
}

    static void
_puls_reposition(_self)
    void*	_self;
{
    METHOD(PopupList, reposition);

    _box_move(&self->position, 1, 1);
    if (! self->aligner)
	self->op->calc_size(self, Columns-1, Rows-1);
    else
    {
	self->op->calc_size(self, self->aligner->limits.width, self->aligner->limits.height);
	self->aligner->op->align(self->aligner, &self->position, self->border);
    }
}

    static void
_puls_redraw(_self)
    void *_self;
{
    int		thumb_heigth;
    int		thumb_pos;
    int		i, idx_filter, idx_model;
    int		row, col, bottom, right;
    int		attr;
    int		attr_norm   = _puls_hl_attrs[PULSATTR_NORMAL].attr;
    int		attr_select = _puls_hl_attrs[PULSATTR_SELECTED].attr;
    int		attr_mark   = _puls_hl_attrs[PULSATTR_MARKED].attr;
    char_u	*text;
    int		item_count;
    LineWriter_T* writer;

    METHOD(PopupList, redraw);

    item_count = self->filter->op->get_item_count(self->filter);

    int hidden = item_count - self->position.height;
    int blank = 0;
    if ((self->first + self->position.height) > item_count)
	/* we have empty lines */
	blank = self->first + self->position.height - item_count;
    hidden += blank;
    int scrollbar = hidden > 0;
    right = _box_right(&self->position);
    bottom = _box_bottom(&self->position);
    if (scrollbar)
    {
	thumb_heigth = self->position.height * self->position.height / (item_count + blank);
	if (thumb_heigth == 0)
	    thumb_heigth = 1;
	thumb_pos = (self->first * (self->position.height - thumb_heigth) + hidden / 2) / hidden;
	if (thumb_pos + thumb_heigth > self->position.height)
	    thumb_pos = self->position.height - thumb_heigth;
    }
    self->border->op->prepare_scrollbar(self->border, item_count);
    self->border->op->draw_top(self->border);

    writer = new_LineWriter();
    writer->offset = self->leftcolumn;
    writer->min_col = self->position.left;
    writer->max_col = _box_right(&self->position);
    writer->tab_size = -2;
    writer->fixed_tabs[0] = -1;
    if (self->column_split)
	writer->op->add_fixed_tab(writer, self->split_width);

    for (i = 0; i < self->position.height; ++i)
    {
	idx_filter = i + self->first;
	attr = (idx_filter == self->current) ? attr_select : attr_norm;
	/* TODO: attr for title items */
	/* TODO: attr for marked items */
	/* TODO: attr for substring match */

	self->border->op->draw_item_left(self->border, i, self->current);
	row = self->position.top + i;
	col = self->position.left;
	if (idx_filter >= item_count)
	{
	    screen_fill(row, row + 1, col, right+1, ' ', ' ', attr);
	    self->border->op->draw_item_right(self->border, i, self->current);
	    continue;
	}

	idx_model = self->filter->op->get_model_index(self->filter, idx_filter);

	if (self->model->op->is_marked(self->model, idx_model))
	{
	    attr = (idx_filter == self->current) ? attr_select : attr_mark;
	}

	text = self->model->op->get_display_text(self->model, idx_model);
	writer->op->write_line(writer, text, row, attr, ' ');

	self->border->op->draw_item_right(self->border, i, self->current);
    }
    self->border->op->draw_bottom(self->border);
    self->need_redraw = 0;

    CLASS_DELETE(writer);

    windgoto(msg_row, msg_col);
}

    static void
_puls_set_current(_self, index)
    void* _self;
    int index;
{
    METHOD(PopupList, set_current);
    int item_count = self->filter->op->get_item_count(self->filter);
    if (index < 0)
	index = 0;
    if (index >= item_count)
	index = item_count - 1;
    self->current = index;
    self->need_redraw |= PULS_REDRAW_CURRENT;
    if (self->current - self->first >= self->position.height)
    {
	self->first = self->current - self->position.height + 1;
	if (self->first < 0)
	    self->first = 0;
	self->need_redraw |= PULS_REDRAW_ALL;
    }
    else if (self->current < self->first)
    {
	self->first = self->current - 3; /* TODO: optional scrolloff for the puls */
	if (self->first < 0)
	    self->first = 0;
	self->need_redraw |= PULS_REDRAW_ALL;
    }
}

    static int
_puls_do_command(_self, command)
    void* _self;
    char_u* command;
{
    METHOD(PopupList, do_command);

    int item_count = self->filter->op->get_item_count(self->filter);
    int idx_model_current = self->filter->op->get_model_index(self->filter, self->current);

    int horz_step = self->position.width / 2; /* TODO: horz_step could be configurable */
    if (horz_step < PULS_MIN_WIDTH / 2)
	horz_step = PULS_MIN_WIDTH / 2;

    if (EQUALS(command, "next-item"))
    {
	if (self->current < item_count-1)
	{
	    self->current += 1;
	    self->need_redraw |= PULS_REDRAW_CURRENT;
	    if (self->current - self->first >= self->position.height)
	    {
		self->first = self->current - self->position.height + 1;
		self->need_redraw |= PULS_REDRAW_ALL;
	    }
	}
	return 1;
    }
    else if (EQUALS(command, "prev-item")) {
	if (self->current > 0)
	{
	    self->current -= 1;
	    self->need_redraw |= PULS_REDRAW_CURRENT;
	    if (self->current < self->first)
	    {
		self->first = self->current;
		self->need_redraw |= PULS_REDRAW_ALL;
	    }
	}
	return 1;
    }
    else if (EQUALS(command, "next-page"))
    {
	if (self->current < item_count-1)
	{
	    self->current += self->position.height;
	    if (self->current >= item_count)
		self->current = item_count-1;
	    self->need_redraw |= PULS_REDRAW_CURRENT;
	    if (self->current - self->first >= self->position.height)
	    {
		self->first += self->position.height;
		if (self->first > self->current)
		    self->first = self->current - self->position.height + 1;
		if (self->first < 0)
		    self->first = 0;
		self->need_redraw |= PULS_REDRAW_ALL;
	    }
	}
	return 1;
    }
    else if (EQUALS(command, "prev-page")) {
	if (self->current > 0)
	{
	    self->current -= self->position.height;
	    if (self->current < 0)
		self->current = 0;
	    self->need_redraw |= PULS_REDRAW_CURRENT;
	    if (self->current < self->first)
	    {
		self->first -= self->position.height;
		if (self->first < 0)
		    self->first = 0;
		self->need_redraw |= PULS_REDRAW_ALL;
	    }
	}
	return 1;
    }
    else if (EQUALS(command, "shift-left")) {
	if (self->leftcolumn > 0)
	{
	    self->leftcolumn -= horz_step;
	    if (self->leftcolumn < 0)
		self->leftcolumn = 0;
	    self->need_redraw |= PULS_REDRAW_ALL;
	}
	return 1;
    }
    else if (EQUALS(command, "shift-right")) {
	/* TODO: limit with max_display_width */
	self->leftcolumn += horz_step;
	self->need_redraw |= PULS_REDRAW_ALL;
	return 1;
    }
    else if (EQUALS(command, "toggle-marked")) {
	int marked = !self->model->op->is_marked(self->model, idx_model_current);
	self->model->op->set_marked(self->model, idx_model_current, marked);
	self->need_redraw |= PULS_REDRAW_CURRENT;
	return 1;
    }
    else if (EQUALS(command, "filter-bs")) {
	if (self->filter)
	{
	    if (self->filter->op->backspace(self->filter))
	    {
		self->filter->op->filter_items(self->filter);
		if (self->current >= self->filter->op->get_item_count(self->filter))
		    self->op->set_current(self, 0);
		if (self->border)
		    self->border->op->set_input_text(self->border, self->filter->text);
		self->need_redraw |= PULS_REDRAW_ALL;
	    }
	}
	return 1;
    }

    return 0;
}

/*
 * Add character 'c' to buffer "buf".
 * Translates special keys, NUL, CSI, K_SPECIAL and multibyte characters.
 * Copied from getchar.c (add_char_buf)
 */
    static char_u*
key_to_str(c, buf)
    int		c;
    char_u*	buf;
{
#ifdef FEAT_MBYTE
    char_u	bytes[MB_MAXBYTES + 1];
    int		len;
    int		i;
#endif
    /*char_u	temp[4];*/

#ifdef FEAT_MBYTE
    if (IS_SPECIAL(c))
	len = 1;
    else
	/* XXX: can mb_char2bytes produce K_SPECIAL, K_CSI or IS_SPECIAL() ? */
	len = (*mb_char2bytes)(c, bytes);
    for (i = 0; i < len; ++i)
    {
	if (!IS_SPECIAL(c))
	    c = bytes[i];
#endif

	if (IS_SPECIAL(c) || c == K_SPECIAL || c == NUL)
	{
	    /* translate special key code into three byte sequence */
	    *buf++ = K_SPECIAL;
	    *buf++ = K_SECOND(c);
	    *buf++ = K_THIRD(c);
	    /*temp[3] = NUL;*/
	}
#ifdef FEAT_GUI
	else if (c == CSI)
	{
	    /* Translate a CSI to a CSI - KS_EXTRA - KE_CSI sequence */
	    *buf++ = CSI;
	    *buf++ = KS_EXTRA;
	    *buf++ = (int)KE_CSI;
	    /*temp[3] = NUL;*/
	}
#endif
	else
	{
	    *buf++ = c;
	    /*temp[1] = NUL;*/
	}
#ifdef FEAT_MBYTE
    }
#endif
    *buf = NUL;
    return buf;
}

    static int
_getkey()
{
    int key;
    ++no_mapping;
    ++allow_keys;
    key = safe_vgetc();
    --no_mapping;
    --allow_keys;
    return key;
}

    static int
_puls_test_loop(pplist, rettv)
    PopupList_T* pplist;
    typval_T*    rettv;
{
    char buf[32];
    SimpleKeymap_T *modemap;
    ItemProvider_T *pmodel;
    WindowBorder_T *pborder;

#define MAX_KEY_SIZE	6*3+1		/* XXX: What is the longest sequence key_to_str can produce? */
#define MAX_SEQ_LEN	8*MAX_KEY_SIZE
    char_u sequence[MAX_SEQ_LEN];	/* current input sequence */
    char_u *ps;
    dictitem_T* seqmap;
    char_u* command;
    int seq_len, key, found, prev_found;

    pborder = pplist->border;
    pmodel = pplist->model;
    ps = pmodel->op->get_title(pmodel);
    if (ps != NULL)
	pplist->op->set_title(pplist, ps);

    modemap = pplist->km_normal;
    ps = sequence;
    *ps = NUL;
    pplist->need_redraw = PULS_REDRAW_ALL;
    found = KM_NOTFOUND;
    for (;;)
    {
	if (pborder)
	{
	    vim_snprintf(buf, 32, "%d/%d/%d", pplist->current + 1,
		    pplist->filter->op->get_item_count(pplist->filter),
		    pmodel->op->get_item_count(pmodel));
	    pborder->op->set_info(pborder, buf);
	}
	if (pplist->need_redraw)
	{
	    pplist->op->redraw(pplist);
	}
	else if (pborder)
	{
	    pborder->op->draw_bottom(pborder);
	}
#if 1   /* XXX: Diagnostic code */
	int	attr = highlight_attr[HLF_PNI];
	int	tl = 0;
	{
	    sprintf(buf, " Key: %d ", key);
	    screen_puts_len(buf, STRLEN(buf), msg_row, msg_col+tl, attr);
	    tl += STRLEN(buf);
	}
	if (modemap == pplist->km_normal)
	{
	    screen_puts_len("NORMAL", 6, msg_row, msg_col+tl, attr);
	    tl += 6;
	}
	if (modemap == pplist->km_filter)
	{
	    screen_puts_len("FILTER: ", 8, msg_row, msg_col+tl, attr);
	    tl += 8;
	    int l = (int)STRLEN(pplist->filter->text);
	    screen_puts_len(pplist->filter->text, l, msg_row, msg_col+tl, attr);
	    tl += l;
	}
	screen_fill(msg_row, msg_row + 1, tl, Columns+1, ' ', ' ', attr);
#endif

	key = _getkey();
	ps = key_to_str(key, ps);
	seq_len = ps - sequence;
	if (seq_len < 1)
	    continue;

	prev_found = found;
	found = modemap->op->find_key(modemap, sequence);
	if (found == KM_PREFIX)
	{
	    if (seq_len > MAX_SEQ_LEN - MAX_KEY_SIZE)
	    {
		ps = sequence;
		*ps = NUL;
		found = KM_NOTFOUND;
	    }
	    continue;
	}
	if (found == KM_NOTFOUND)
	{
	    if (modemap == pplist->km_filter && prev_found != KM_PREFIX && !IS_SPECIAL(key))
	    {
		if (pplist->filter->op->add_text(pplist->filter, sequence))
		{
		    pplist->filter->op->filter_items(pplist->filter);
		    if (pplist->current >= pplist->filter->op->get_item_count(pplist->filter))
			pplist->op->set_current(pplist, 0);
		    if (pborder)
			pborder->op->set_input_text(pborder, pplist->filter->text);
		    pplist->need_redraw |= PULS_REDRAW_ALL;
		    /* TODO: if (autosize) pplist->need_redraw |= PULS_REDRAW_RESIZE; */
		}
	    }
	    ps = sequence;
	    *ps = NUL;
	    continue;
	}

	command = modemap->op->get_command(modemap, sequence, 0 /* don't create a copy */ );
	ps = sequence;
	*ps = NUL;
	if (! command) /* only true if there is a bug somewhere */
	{
	    continue;
	}

	if (EQUALS(command, "quit"))
	    break;
	else if (EQUALS(command, "accept"))
	    break;
	else if (EQUALS(command, "modeswitch:normal"))
	{
	    modemap = pplist->km_normal;
	    if (pborder)
	    {
		pborder->op->set_mode_text(pborder, "");
		pborder->op->set_input_active(pborder, 0);
	    }
	    pplist->need_redraw |= PULS_REDRAW_FRAME;
	    continue;
	}
	else if (EQUALS(command, "modeswitch:filter"))
	{
	    modemap = pplist->km_filter;
	    if (pborder)
	    {
		pborder->op->set_mode_text(pborder, "/");
		pborder->op->set_input_text(pborder, pplist->filter->text);
		pborder->op->set_input_active(pborder, 1);
	    }
	    pplist->need_redraw |= PULS_REDRAW_FRAME;
	    continue;
	}
	else if (pplist->op->do_command(pplist, command))
	{
	    continue;
	}
	else if (EQUALS(command, "markomahnic"))
	{
	    int	attr = highlight_attr[HLF_PNI];
	    char author[] = "The Author of Popup List is Marko Mahnič.";
	    screen_puts_len(author, (int)STRLEN(author), msg_row, msg_col, attr);
	}
	else
	{
	    char_u* next_command;
	    dict_T* status = dict_alloc();
	    pplist->op->prepare_result(pplist, status);
	    ++status->dv_refcount;
	    next_command = pmodel->op->handle_command(pmodel, pplist, command, status);
	    dict_unref(status);
	}
    }

    /* TODO: figure out the correct way to hide the box; consider that there could
     * be multiple overlapping boxes */
    redraw_all_later(NOT_VALID);
    update_screen(NOT_VALID);

    int rv = OK;
    dict_T	*d = dict_alloc();

    if (!d)
	rv = FAIL;
    else
    {
	rettv->vval.v_dict = d;
	rettv->v_type = VAR_DICT;
	++d->dv_refcount;

	dict_add_nr_str(d, "status", 0, command);

	if (EQUALS(command, "accept"))
	    pplist->op->prepare_result(pplist, d);
    }

    return rv;
}

/* TODO: the interface should be: popuplist({items} [, {title}] [, {options}])
 *    Process the {items} in a popup window. {items} is a list or a string with 
 *    the name of an internal list (eg. buffers). An optional window title
 *    can be set with {title} or with an entry in the {options} dictionary.
 *    The following entries are supported:
 *	options.title
 *	    The title of the window.
 *	options.commads
 *	    The commands that the list can handle.
 *	options.keymap
 *	    The commands can be assigned to keysequences.
 *	options.pos
 *	    The placement of the popup window.
 *	options.filter
 *	    The filtering algorithm.
 *
 *  When the list processing is done, the function returns a result in a
 *  dictionary:
 *	rv.status	'accept' or 'cancel'  XXX: ?
 *	rv.current	currently selected item
 *	rv.marked	list of marked item indices
 *
 *  More information is in |popuplst.txt|.
 *
 *  Use examples:
 *    let alist = ["1", "2", "three"]
 *    let rv = popuplist("buffers")
 *    let rv = popuplist(alist, "Some list")
 *    let rv = popuplist(alist, "Some list", { 'pos': '11' })
 *    let rv = popuplist(alist, { 'title': "Some list", 'pos': '11' })
 *    let rv = popuplist({ 'items': alist, 'title': "Some list", 'pos': '11' })
 *
 *    XXX: ATM: {items} is a list, a string or a dictionary.
 */
    int
puls_test(argvars, rettv)
    typval_T	*argvars;
    typval_T	*rettv;
{
    BoxAligner_T* aligner = NULL;
    PopupList_T* pplist = NULL;
    ItemFilter_T* filter = NULL;
    ItemProvider_T* model = NULL;
    dictitem_T *option, *di;
    DictIterator_T itd;
    char_u* special_items = NULL;
    list_T* items = NULL;
    dict_T* options = NULL;

    _init_vtables();
    _update_hl_attrs();
    _dicti_init(&itd);

    LOG(("---------- New listbox -----------"));

    if (argvars[0].v_type == VAR_DICT)
    {
	options = argvars[0].vval.v_dict;
	option = dict_find(argvars[0].vval.v_dict, "items", -1L);
	if (option)
	{
	    if (option->di_tv.v_type == VAR_LIST)
	    {
		items = option->di_tv.vval.v_list;
	    }
	    else if (option->di_tv.v_type == VAR_STRING)
	    {
		special_items = option->di_tv.vval.v_string;
	    }
	}
    }
    else if (argvars[0].v_type == VAR_LIST)
    {
	items = argvars[0].vval.v_list;
    }
    else if (argvars[0].v_type == VAR_STRING)
    {
	special_items = argvars[0].vval.v_string;
    }

    /* prepare the provider */
    if (special_items)
    {
#ifdef FEAT_POPUPLIST_BUFFERS
	if (EQUALS(special_items, "buffers"))
	{
	    BufferItemProvider_T* bmodel = new_BufferItemProvider();
	    bmodel->op->list_buffers(bmodel);
	    model = (ItemProvider_T*) bmodel;
	    LOG(("Buffers"));
	}
#endif
	if (EQUALS(special_items, "pulslog"))
	{
	    LOG(("PULS LOG"));
	    VimlistItemProvider_T* vlmodel = new_VimlistItemProvider();
	    vlmodel->op->set_list(vlmodel, &PULSLOG);
	    model = (ItemProvider_T*) vlmodel;
	}

	if (! model)
	{
	    /* TODO: message: invalid item provider '%special_items' */
	    return FAIL;
	}
    }
    else if (! items)
    {
	static char_u a[] = "first line:\tThe popup list (PULS)";
	static char_u b[] = "line2:\tis a C implementation";
	static char_u c[] = "line3:\tof the popuplist";
	static char_u d[] = "line4:\tfrom vimuiex";
	static char_u e[] = "last line:\tthat was implemented in Python.";
	int i;

	model = new_ItemProvider();
	for (i = 0; i < 3; i++)
	{
	    model->op->append_pchar_item(model, a, ITEM_SHARED);
	    model->op->append_pchar_item(model, b, ITEM_SHARED);
	    model->op->append_pchar_item(model, c, ITEM_SHARED);
	    model->op->append_pchar_item(model, d, ITEM_SHARED);
	    model->op->append_pchar_item(model, e, ITEM_SHARED);
	}
    }
    else
    {
	VimlistItemProvider_T* vlmodel = new_VimlistItemProvider();
	vlmodel->op->set_list(vlmodel, items);
	model = (ItemProvider_T*) vlmodel;
    }

    if (! model)
    {
	/* TODO: message: no items defined */
	return FAIL;
    }

    aligner = new_BoxAligner();
    aligner->op->set_limits(aligner, 0, 0, Columns - 1, Rows - 3);

    filter = new_ItemFilter();
    filter->model = model;

    pplist = new_PopupList();
    pplist->model = model;
    model->op->default_keymap(model, pplist);

    pplist->column_split = 1; /* TODO: option */

    if (options)
    {
	option = dict_find(options, "keymap", -1L);
	if (option && option->di_tv.v_type == VAR_DICT)
	{
	    /* create the keymaps */
	    for(di = itd.op->begin(&itd, option->di_tv.vval.v_dict); di != NULL; di = itd.op->next(&itd))
	    {
		if (di->di_tv.v_type != VAR_DICT)
		{
		    /* TODO: WARN - invalid keymap type, should be dict */
		    continue;
		}
		pplist->op->map_keys(pplist, di->di_key, di->di_tv.vval.v_dict);
	    }
	}

	option = dict_find(options, "commands", -1L);
	if (model && option && option->di_tv.v_type == VAR_DICT)
	{
	    /* XXX: for now, assume the options are const while the popup is running.
	     * Note that a callback could modify the options!
	     */
	    model->commands = option->di_tv.vval.v_dict;
	}

	option = dict_find(options, "pos", -1L);
	if (option)
	{
	    if (option->di_tv.v_type == VAR_STRING)
	    {
		aligner->op->parse_screen_pos(aligner, option->di_tv.vval.v_string);
	    }
	    else if (option->di_tv.v_type == VAR_DICT)
		aligner->op->set_align_params(aligner, option->di_tv.vval.v_dict);
	}

	option = dict_find(options, "filter", -1L); /* TODO */

	/* Currently only special providers need to have options */
	if (model)
	{
	    model->op->read_options(model, options);
	}
    }

    pplist->filter  = filter;
    pplist->aligner = aligner;
    pplist->op->reposition(pplist);

    /* process the list */
    int rv = _puls_test_loop(pplist, rettv);

    CLASS_DELETE(pplist);
    CLASS_DELETE(aligner);
    CLASS_DELETE(filter);
    CLASS_DELETE(model);

    return rv;
}

#endif
