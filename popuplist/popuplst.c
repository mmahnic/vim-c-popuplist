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
#define PULSATTR_DISABLED	6
#define PULSATTR_DISABLED_SEL	7
#define PULSATTR_BORDER		8
#define PULSATTR_SCROLL_BAR	9
#define PULSATTR_SCROLL_THUMB	10
#define PULSATTR_SCROLL_SPACE	11 /* special attr when scrollbar page is rendered with spaces */
#define PULSATTR_INPUT		12
#define PULSATTR_INPUT_ACTIVE	13
#define PULSATTR_SHORTCUT	14
#define PULSATTR_SHORTCUT_SEL	15
#define PULSATTR_HL_FILTER	16
#define PULSATTR_HL_SEARCH	17
#define PULSATTR_HL_USER	18

static _puls_hl_attrs_T _puls_hl_attrs[] = {
    { "PulsNormal",	    0, HLF_PNI },
    { "PulsSelected",	    0, HLF_PSI },
    { "PulsTitleItem",	    0, -PULSATTR_NORMAL },
    { "PulsTitleItemSel",   0, -PULSATTR_SELECTED },
    { "PulsMarked",	    0, HLF_PSI },
    { "PulsMarkedSel",	    0, -PULSATTR_SELECTED },
    { "PulsDisabled",	    0, HLF_PNI },
    { "PulsDisabledSel",    0, -PULSATTR_SELECTED },
    { "PulsBorder",	    0, -PULSATTR_NORMAL },
    { "PulsScrollBar",	    0, -PULSATTR_BORDER },
    { "PulsScrollThumb",    0, HLF_PST },
    { "PulsScrollBarSpace", 0, HLF_PSB },
    { "PulsInput",	    0, -PULSATTR_BORDER },
    { "PulsInputActive",    0, HLF_PSI },
    { "PulsShortcut",       0, HLF_PSI },
    { "PulsShortcutSel",    0, HLF_PSI },
    { "PulsHlFilter",       0, HLF_PNI },
    { "PulsHlSearch",       0, HLF_I },
    { "PulsHlUser",         0, HLF_L }
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

#include "popupls_.ci" /* created by mmoocc.py from class definitions in [ooc] blocks */
#include "puls_st.c"
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
  const ITEM_SHARED	= 0x01;
  const ITEM_MARKED	= 0x02;
  const ITEM_TITLE	= 0x04;
  const ITEM_DISABLED	= 0x08;
  const ITEM_SEPARATOR	= 0x10;
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

/* [ooc]
 *
  class ItemProvider(object) [iprov]
  {
    SegmentedGrowArray* items;
    dict_T*		commands;  // commands defined in vim-script
    char_u*		title;
    void	init();
    void	destroy();
    void	read_options(dict_T* options);
    void	on_start();	    // called before the items are displayed for the first time
    void	clear_items();
    int		get_item_count();
    PopupItem_T* get_item(int item);

    // Values returned by append_pchar_item should be considered temporary!
    // @param shared=0 => will be free()-d
    PopupItem_T* append_pchar_item(char_u* text, int shared);
    char_u*	get_display_text(int item);

    // TODO: filternig can be implemented with a separate string or with a
    // pointer into the original string and the size of the substring. We could
    // assume that the part of the string to be filtered is contiguous.
    char_u*	get_filter_text(int item);
    char_u*	get_path_text();
    char_u*	get_title();
    void	set_title(char_u* title);
    void	set_marked(int item, int marked);
    uint	has_flag(int item, uint flag);

    // select_item()
    // @returns:
    //	 0 - let the caller handle it
    //	 1 - remain in the event loop; update the items, they may have changed
    //	-1 - exit the loop; return the result 'done' (executed by the handler)
    int		select_item(int item);
    // select_parent()
    // @returns:
    //   -1 - no parent
    //    i - index of current sublist in the parent list
    int		select_parent();
    int		sort_items(ItemComparator* cmp); // returns TRUE if qsort was called

    // the provider may add a extra information to the result or change the existing information
    void	update_result(dict_T* status);
    // the provider handles the command and returns the next command
    char_u*	handle_command(PopupList* puls, char_u* command);

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
    self->items = new_SegmentedGrowArrayP(sizeof(PopupItem_T), &_ppit_destroy);
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

    CLASS_DELETE(self->items);

    END_DESTROY(ItemProvider);
}

    static void
_iprov_read_options(_self, options)
    void* _self;
    dict_T* options;
{
    METHOD(ItemProvider, read_options);
}

    static void
_iprov_on_start(_self)
    void* _self;
{
    METHOD(ItemProvider, on_start);
}

    static void
_iprov_clear_items(_self)
    void* _self;
{
    int i;
    METHOD(ItemProvider, clear_items);
    /* clear deletes cached text from items */
    self->items->op->clear(self->items);
}

    static int
_iprov_get_item_count(_self)
    void* _self;
{
    METHOD(ItemProvider, get_item_count);
    return self->items->len;
}

    static PopupItem_T*
_iprov_get_item(_self, item)
    void* _self;
    int item;
{
    METHOD(ItemProvider, get_item);
    if (item < 0 || item >= self->items->len)
	return NULL;
    return (PopupItem_T*) (self->items->op->get_item(self->items, item));
}

    static PopupItem_T*
_iprov_append_pchar_item(_self, text, shared)
    void* _self;
    char_u* text;
    int shared;
{
    METHOD(ItemProvider, append_pchar_item);
    PopupItem_T* pitnew;
    pitnew = self->items->op->get_new_item(self->items);
    if (pitnew)
    {
	init_PopupItem(pitnew);
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
    PopupItem_T* pit;
    pit = self->items->op->get_item(self->items, item);
    return pit ? pit->text : NULL;
}

    static char_u*
_iprov_get_filter_text(_self, item)
    void* _self;
    int item;
{
    METHOD(ItemProvider, get_filter_text);
    PopupItem_T* pit;
    pit = self->items->op->get_item(self->items, item);
    if (!pit || !pit->text)
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
_iprov_set_title(_self, title)
    void*   _self;
    char_u* title;
{
    METHOD(ItemProvider, set_title);
    _str_assign(&self->title, title);
}

    static void
_iprov_set_marked(_self, item, marked)
    void* _self;
    int item;
    int marked;
{
    METHOD(ItemProvider, set_marked);
    PopupItem_T* pit;
    pit = self->items->op->get_item(self->items, item);
    if (!pit)
	return;
    if (marked) pit->flags |= ITEM_MARKED;
    else pit->flags &= ~ITEM_MARKED;
}

    static uint
_iprov_has_flag(_self, item, flag)
    void* _self;
    int item;
    uint flag;
{
    METHOD(ItemProvider, has_flag);
    PopupItem_T* pit;
    pit = self->items->op->get_item(self->items, item);
    if (!pit)
	return 0;

    return pit->flags & flag;
}

    static int
_iprov_select_item(_self, item)
    void* _self;
    int item;
{
    METHOD(ItemProvider, select_item);
    return 0; /* a leaf item, execute */
    /* TODO: VimlistItemProvider calls handle_command('itemselected') when it
     * is defined in options. Something similar should be done in
     * select_parent: call handle_command('parentselected') if defined. An
     * alternative is to keep the contents of the list on stack before
     * executing 'itemselected'. */
}

    static int
_iprov_select_parent(_self)
    void* _self;
{
    METHOD(ItemProvider, select_parent);
    return -1; /* no parent, ignore */
}

    static void
_iprov_update_result(_self, status)
    void*	    _self;
    dict_T*	    status;   /* the status of the popup list: selected item, marked items, etc. */
{
    METHOD(ItemProvider, update_result);
}

    static char_u*
_iprov_handle_command(_self, puls, command)
    void*	    _self;
    PopupList_T*    puls;
    char_u*	    command;
{
    METHOD(ItemProvider, handle_command);
    dictitem_T* icmd;

    if (!self->commands || !command)
	return NULL;

    icmd = dict_find(self->commands, command, -1L);
    if (!icmd)
	return NULL;

    {
	typval_T    argv[2 + 1]; /* command, status */
	typval_T    rettv;
	int	    argc = 0;
	listitem_T  *item;
	int	    dummy;
	dict_T	    *selfdict = NULL;
	char_u*	    fn = icmd->di_tv.vval.v_string;
	dict_T*	    status = NULL;

	status = dict_alloc();
	++status->dv_refcount;
	puls->op->prepare_result(puls, status);

	argv[0].v_type = VAR_STRING;
	argv[0].vval.v_string = command;
	++argc;

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
    ItemComparator_T* cmp;
{
    METHOD(ItemProvider, sort_items);
    int ni;
    ni = self->op->get_item_count(self);
    if (ni < 2)
	return 0;

    self->items->op->sort(self->items, cmp);
    return 1;
}

/* [ooc]
 *
  class VimlistItemProvider(ItemProvider) [vlprov]
  {
    list_T*	vimlist;
    int		_refcount; // how many times have we referenced the list
    // @var skip_leading is the number of characters to skip in the text
    // returned by get_display_text. The leading characters can be used to pass
    // additional information to each item (eg. title, disabled, marked,
    // hierarchical level). At most 2 leading characters can be used.
    int		skip_leading;
    void	init();
    void	set_list(list_T* vimlist);
    char_u*	get_display_text(int item);
    void	destroy();
  };
*/

    static void
_vlprov_init(_self)
    void* _self;
{
    METHOD(VimlistItemProvider, init);
    self->vimlist = NULL;
    self->_refcount = 0;
    self->skip_leading = 0;
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

    static char_u*
_vlprov_get_display_text(_self, item)
    void* _self;
    int item;
{
    METHOD(VimlistItemProvider, get_display_text);
    PopupItem_T* pit;
    char_u* text;
    pit = self->op->get_item(self, item);
    if (!pit)
	return NULL;
    text = pit->text;
    if (text && self->skip_leading > 0)
    {
	if (*text) text++;
	if (self->skip_leading > 1 && *text) text++;
    }
    return text;
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

/* TODO: #ifdef FEAT_POPUPLIST_MENU*/
#include "puls_pm.c"
/*#endif*/

/* [ooc]
 *
  // Observer pattern
  typedef int (*MethodCallback_Fn)(void* _self, void* _data);
  struct NotificationCallback [ntfcb]
  {
    NotificationCallback* next;
    void*   instance_self;
    MethodCallback_Fn callback;
    void init();
    int  call(void* data);
  };

  class NotificationList [ntlst]
  {
    NotificationCallback* observers;
    ListHelper lst_observers;
    void init();
    void destroy();
    void notify(void* _data);
    void add(void* instance, MethodCallback_Fn callback);
    void remove_obj(void* instance);
    void remove_cb(MethodCallback_Fn callback);
  };
*/

    static void
_ntfcb_init(_self)
    void* _self;
{
    METHOD(NotificationCallback, init);
    self->next = NULL;
    self->instance_self = NULL;
    self->callback = NULL;
}

    static int
_ntfcb_call(_self, _data)
    void* _self;
    void* _data;
{
    METHOD(NotificationCallback, call);
    if (! self->callback)
	return 0;
    return (*self->callback)(self->instance_self, _data);
}

    static void
_ntlst_init(_self)
    void* _self;
{
    METHOD(NotificationList, init);
    self->observers = NULL;
    self->lst_observers.first = (void**)&self->observers;
    self->lst_observers.offs_next = offsetof(NotificationCallback_T, next);
    /* items don't need destruction, so we don't set lst_observers.fn_destroy */
}

    static void
_ntlst_destroy(_self)
    void* _self;
{
    METHOD(NotificationList, destroy);
    NotificationCallback_T* pit;
    while (self->observers)
    {
	pit = self->observers;
	self->observers = self->observers->next;
	vim_free(pit);
    }
    END_DESTROY(NotificationList);
}

    static void
_ntlst_notify(_self, _data)
    void* _self;
    void* _data;
{
    METHOD(NotificationList, notify);
    NotificationCallback_T* pit;
    pit = self->observers;
    while (pit)
    {
	_ntfcb_call(pit, _data);
	pit = pit->next;
    }
}

    static void
_ntlst_add(_self, instance, callback)
    void* _self;
    void* instance;
    MethodCallback_Fn callback;
{
    METHOD(NotificationList, add);
    NotificationCallback_T *pnew;
    pnew = new_NotificationCallback();
    pnew->instance_self = instance;
    pnew->callback = callback;
    self->lst_observers.op->add_tail(&self->lst_observers, pnew);
}

    static int
_fn_match_callback_instance(matcher, item)
    ItemMatcher_T* matcher;
    NotificationCallback_T* item;
{
    return (matcher->extra == item->instance_self);
}

    static void
_ntlst_remove_obj(_self, instance)
    void* _self;
    void* instance;
{
    METHOD(NotificationList, remove_obj);
    ItemMatcher_T cmp;
    init_ItemMatcher(&cmp);
    cmp.fn_match = &_fn_match_callback_instance;
    cmp.extra = instance;
    self->lst_observers.op->delete_all(&self->lst_observers, &cmp);
}

    static int
_fn_match_callback_callback(matcher, item)
    ItemMatcher_T* matcher;
    NotificationCallback_T* item;
{
    return (matcher->extra == item->callback);
}

    static void
_ntlst_remove_cb(_self, callback)
    void* _self;
    MethodCallback_Fn callback;
{
    METHOD(NotificationList, remove_cb);
    ItemMatcher_T cmp;
    init_ItemMatcher(&cmp);
    cmp.fn_match = &_fn_match_callback_callback;
    cmp.extra = callback;
    self->lst_observers.op->delete_all(&self->lst_observers, &cmp);
}

/* [ooc]
 *
  // Basic line edit 'widget'. ATM editing is possible only at the end of line.
  class LineEdit(object) [lned]
  {
    char_u* text;
    int	    len;
    int	    size;
    int	    max_len;
    NotificationList change_obsrvrs;
    void    init();
    void    destroy();
    void    set_text(char_u* ptext);
    int	    add_text(char_u* ptext);
    int	    backspace();
  };
*/

    static void
_lned_init(_self)
    void* _self;
{
    METHOD(LineEdit, init);
    self->text = NULL;
    self->len = 0;
    self->size = 0;
    self->max_len = 0; /* unlimited */
}

    static void
_lned_destroy(_self)
    void* _self;
{
    METHOD(LineEdit, destroy);
    _str_free(&self->text);
    END_DESTROY(LineEdit);
}

    static int
_lned_add_text(_self, ptext)
    void* _self;
    char_u* ptext;
{
    METHOD(LineEdit, add_text);
    int nlen = self->len + STRLEN(ptext);

    if (! ptext || ! *ptext)
	return 0;

    if (self->max_len > 0 && nlen > self->max_len)
	return 0;

    if (nlen >= self->size)
    {
	self->size *= 2;
	if (self->size <= nlen)
	    self->size = ((nlen + 1) / 32 + 1) * 32;
	if (! self->text)
	{
	    self->text = (char_u*) alloc(self->size);
	    *self->text = NUL;
	}
	else
	    self->text = (char_u*) vim_realloc(self->text, self->size);
    }

    strcat(self->text, ptext);
    self->len = nlen;
    return 1;
}

    static void
_lned_set_text(_self, ptext)
    void* _self;
    char_u* ptext;
{
    METHOD(LineEdit, set_text);
    _str_free(&self->text);
    self->len = 0;
    self->size = 0;
    self->op->add_text(self, ptext);
}

    static int
_lned_backspace(_self)
    void* _self;
{
    METHOD(LineEdit, backspace);
    char_u* last_char;
    if (! self->text || STRLEN(self->text) < 1)
	return 0;

    last_char = self->text + STRLEN(self->text);
    mb_ptr_back(self->text, last_char);
    *last_char = NUL;
    return 1;
}

/* [ooc]
 *
  const MAX_FILTER_SIZE = 127;
  class ISearch(object) [isrch]
  {
    char_u  text[MAX_FILTER_SIZE + 1];
    int	    start;    // start index for incremental search
    void    init();
    void    destroy();
    void    set_text(char_u* ptext);
    int	    match(char_u* ptext);
  };
*/

    static void
_isrch_init(_self)
    void* _self;
{
    METHOD(ISearch, init);
    self->text[0] = NUL;
    self->start = 0;
}

    static void
_isrch_destroy(_self)
    void* _self;
{
    METHOD(ISearch, destroy);
    END_DESTROY(ISearch);
}

    static void
_isrch_set_text(_self, ptext)
    void* _self;
    char_u* ptext;
{
    METHOD(ISearch, set_text);

    if (! ptext)
	*self->text = NUL;
    else
    {
	STRNCPY(self->text, ptext, MAX_FILTER_SIZE);
	self->text[MAX_FILTER_SIZE] = NUL;
    }
}

    static int
_isrch_match(_self, haystack)
    void* _self;
    char_u* haystack;
{
    METHOD(ISearch, match);
    char *p;
    if (! haystack || ! *haystack)
	return 0;
    if (! *self->text)
	return 1;
    p = _stristr(haystack, self->text);
    if (! p)
	return 0;
    return 1;
}

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
  class FltComparator_Score(ItemComparator) [flcmpscr]
  {
    ItemProvider* model;
    void  init();
    int   compare(void* pia, void* pib);
  };

  class ItemFilter(object) [iflt]
  {
    ItemProvider* model;
    char_u  text[MAX_FILTER_SIZE + 1];
    // array(int) items;  // indices of items in the model
    SegmentedGrowArray* items; // indices of items in the model
    void    init();
    void    destroy();
    void    set_text(char_u* ptext);
    void    filter_items();
    int	    match(char_u* needle, char_u* haystack);
    int	    get_item_count();
    int	    get_model_index(int index);
  };
*/

    static void
_flcmpscr_init(_self)
    void* _self;
{
    METHOD(FltComparator_Score, init);
    self->model = NULL;
}

    static int
_flcmpscr_compare(_self, pia, pib)
    void* _self;
    void* pia;
    void* pib;
{
    /* pia and pib are pointers to indices of items in the model */
    METHOD(FltComparator_Score, compare);
    PopupItem_T *pa, *pb;
    if (!self->model)
	return 0;
    pa = self->model->op->get_item(self->model, *(int*)pia);
    pb = self->model->op->get_item(self->model, *(int*)pib);
    if (!pa)
    {
	if(!pb)
	    return 0;
	return 1;
    }
    else if (!pb)
	return -1;
    if (pa->filter_score < pb->filter_score) return self->reverse ? 1 : -1;
    if (pa->filter_score > pb->filter_score) return self->reverse ? -1 : 1;
    return 0;
}

    static void
_iflt_init(_self)
    void* _self;
{
    METHOD(ItemFilter, init);
    self->model = NULL;
    self->text[0] = NUL;
    self->items = new_SegmentedGrowArrayP(sizeof(int), NULL);
}

    static void
_iflt_destroy(_self)
    void* _self;
{
    METHOD(ItemFilter, destroy);
    self->model = NULL; /* filter doesn't own the model */
    CLASS_DELETE(self->items);
    END_DESTROY(ItemFilter);
}

    static void
_iflt_set_text(_self, ptext)
    void* _self;
    char_u* ptext;
{
    METHOD(ItemFilter, set_text);

    if (! ptext)
	*self->text = NUL;
    else
    {
	STRNCPY(self->text, ptext, MAX_FILTER_SIZE);
	self->text[MAX_FILTER_SIZE] = NUL;
    }
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
    FltComparator_Score_T cmp;
    int item_count, i;
    int *pmi;

    self->items->op->clear(self->items);
    if (STRLEN(self->text) < 1)
	return;

    item_count = self->model->op->get_item_count(self->model);
    for(i = 0; i < item_count; i++)
    {
	int score = self->op->match(self, self->text, self->model->op->get_filter_text(self->model, i));
	pit = self->model->op->get_item(self->model, i); /* TODO: set-item-score() */
	if (pit)
	    pit->filter_score = score;
	if (score <= 0)
	    continue;

	pmi = (int*) self->items->op->get_new_item(self->items);
	if (pmi)
	    *pmi = i;
    }

    init_FltComparator_Score(&cmp);
    cmp.model = self->model;
    cmp.reverse = 1;
    self->items->op->sort(self->items, (ItemComparator_T*)&cmp);
}

    static int
_iflt_get_item_count(_self)
    void* _self;
{
    METHOD(ItemFilter, get_item_count);
    if (STRLEN(self->text) < 1)
	return self->model->op->get_item_count(self->model);

    return self->items->len;
}

    static int
_iflt_get_model_index(_self, index)
    void* _self;
    int index;
{
    METHOD(ItemFilter, get_model_index);
    if (STRLEN(self->text) < 1)
	return index;

    if (index < 0 || index >= self->items->len)
	return -1;

    return *(int*) self->items->op->get_item(self->items, index);
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

    init_Box(&self->limits);
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
     int	has_insert; // kmap is used for text insertion
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
    self->has_insert = 0;
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
    char_u seq[128]; /* XXX: remove from stack */
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
    dictitem_T* pi;
    if (!sequence || !command)
	return;

    /* TODO: Check for conflicting sequences */
    pi = dict_find(self->key2cmd, sequence, -1);
    if (pi)
	dictitem_remove(self->key2cmd, pi); /* TODO: make dictitem_remove non-static in eval.c */
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

    init_DictIterator(&itkeys);
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
    int     input_active;   // input field is active, special display
    LineEdit*  line_edit;   // input line to be displayed in the border; owned by popuplist

    void init();
    void destroy();
    void set_title(char_u* title);
    void set_mode_text(char_u* mode); 
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
    self->input_active = 0;
    self->line_edit = NULL;
}

    static void
_wbor_destroy(_self)
    void* _self;
{
    METHOD(WindowBorder, destroy);
    self->inner_box = NULL;	/* frame doesn't own the box */
    self->line_edit = NULL;	/* frame doesn't own the line_edit */
    _str_free(&self->title);
    _str_free(&self->info);
    _str_free(&self->mode);
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
    char_u buf[16]; /* XXX: small, but remove from stack, anyway */
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
    char_u* input;
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
    if (self->line_edit)
	input = self->line_edit->text;
    else
	input = NULL;

    if (self->input_active && self->line_edit)
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
    writer->op->write_line(writer, input, row, attr, ch);
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
  // TODO: use the flags correctly!!!
  const PULS_REDRAW_CURRENT = 0x01;
  const PULS_REDRAW_FRAME   = 0x02;
  const PULS_REDRAW_ALL     = 0x0f;
  // clear the screen before drawing the puls; implies REDRAW_ALL
  const PULS_REDRAW_CLEAR   = 0x10;
  // resize before drawing; implies REDRAW_CLEAR
  const PULS_REDRAW_RESIZE  = 0x20;
  class PopupList(object) [puls]
  {
    ItemProvider*   model;	// items of the displayed puls
    ItemFilter*	    filter;	// selects a subset of model->items
    ISearch*	    isearch;	// searches for a string in a filtered subset
    SimpleKeymap*   km_normal;
    SimpleKeymap*   km_filter;
    SimpleKeymap*   km_search;
    BoxAligner*	    aligner;	// positions the list box on the screen
    WindowBorder*   border;
    LineEdit*	    line_edit;
    ShortcutHighlighter* hl_menu;
    ISearchHighlighter*  hl_user;   // set in options
    ISearchHighlighter*  hl_filter; // TODO: depends on the selected filter
    ISearchHighlighter*  hl_isearch;
    Highlighter*	 hl_chain;  // chain of highlighters, updated in update_hl_chain()
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
    void    read_options(dict_T* options);
    void    default_keymap();
    void    map_keys(char_u* kmap_name, dict_T* kmap);
    int	    calc_size(int limit_width, int limit_height);
    void    reposition();
    void    update_hl_chain();
    void    redraw();
    int	    do_command(char_u* command);
    void    prepare_result(dict_T* result);
    void    set_title(char_u* title);
    void    set_current(int index);
    int	    do_isearch(); // perfrom isearch with the current isearch settings
    int	    on_filter_change(void* data);   // callback to update filter when input changes
    int	    on_isearch_change(void* data);  // callback to uptate isearch when input changes
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
    self->isearch = new_ISearch();
    self->km_normal = new_SimpleKeymap();
    self->km_normal->op->set_name(self->km_normal, "normal"); /* TODO: add parameter: mode_indicator, eg. "/" */
    self->km_filter = new_SimpleKeymap();
    self->km_filter->op->set_name(self->km_filter, "filter");
    self->km_search = new_SimpleKeymap();
    self->km_search->op->set_name(self->km_search, "search");
    init_Box(&self->position);
    self->border = new_WindowBorder();
    self->border->inner_box = &self->position;
    self->line_edit = new_LineEdit();
    self->border->line_edit = self->line_edit;

    self->op->default_keymap(self);

    self->hl_chain = NULL;
    self->hl_menu = NULL;
    self->hl_user = NULL;
    self->hl_isearch = new_ISearchHighlighter();
    self->hl_isearch->match_attr = _puls_hl_attrs[PULSATTR_HL_SEARCH].attr;
    self->hl_filter = new_ISearchHighlighter();
    self->hl_filter->match_attr = _puls_hl_attrs[PULSATTR_HL_FILTER].attr;
    /* TODO: hl_menu should be created only in menu mode */
    self->hl_menu = new_ShortcutHighlighter();
    self->hl_menu->shortcut_attr = _puls_hl_attrs[PULSATTR_SHORTCUT].attr;
}

    static void
_puls_destroy(_self)
    void* _self;
{
    METHOD(PopupList, destroy);
    self->model = NULL;	    /* puls doesn't own the model */
    self->filter = NULL;    /* puls doesn't own the filter */
    self->aligner = NULL;   /* puls doesn't own the aligner */
    self->hl_chain = NULL;  /* points to one of the highlighters */

    CLASS_DELETE(self->km_normal);
    CLASS_DELETE(self->km_filter);
    CLASS_DELETE(self->km_search);
    CLASS_DELETE(self->border);
    CLASS_DELETE(self->line_edit);
    CLASS_DELETE(self->hl_user);
    CLASS_DELETE(self->hl_menu);
    CLASS_DELETE(self->hl_isearch);
    CLASS_DELETE(self->hl_filter);
    CLASS_DELETE(self->isearch);
    _str_free(&self->title);

    END_DESTROY(PopupList);
}

    static void
_puls_read_options(_self, options)
    void* _self;
    dict_T* options;
{
    METHOD(PopupList, read_options);
    dictitem_T *option, *di;
    DictIterator_T itd;

    option = dict_find(options, "keymap", -1L);
    if (option && option->di_tv.v_type == VAR_DICT)
    {
	/* create the keymaps */
	init_DictIterator(&itd);
	for(di = itd.op->begin(&itd, option->di_tv.vval.v_dict); di != NULL; di = itd.op->next(&itd))
	{
	    if (di->di_tv.v_type != VAR_DICT)
	    {
		/* TODO: WARN - invalid keymap type, should be dict */
		continue;
	    }
	    self->op->map_keys(self, di->di_key, di->di_tv.vval.v_dict);
	}
    }

    option = dict_find(options, "pos", -1L);
    if (option && self->aligner)
    {
	if (option->di_tv.v_type == VAR_STRING)
	{
	    self->aligner->op->parse_screen_pos(self->aligner, option->di_tv.vval.v_string);
	}
	else if (option->di_tv.v_type == VAR_DICT)
	    self->aligner->op->set_align_params(self->aligner, option->di_tv.vval.v_dict);
    }

    option = dict_find(options, "filter", -1L); /* TODO: select filtering mode */

    option = dict_find(options, "highlight", -1L);
    if (option && option->di_tv.v_type == VAR_STRING)
    {
	if (! self->hl_user)
	    self->hl_user = new_ISearchHighlighter();
	self->hl_user->match_attr = _puls_hl_attrs[PULSATTR_HL_USER].attr;
	self->hl_user->op->set_pattern(self->hl_user, option->di_tv.vval.v_string);
    }

    option = dict_find(options, "current", -1L);
    if (option && option->di_tv.v_type == VAR_NUMBER)
    {
	self->current = option->di_tv.vval.v_number;
	if (self->current < 0)
	    self->current = 0;
    }
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
    int i;

    modemap = self->km_normal;
    modemap->op->clear_all_keys(modemap);
    modemap->op->set_key(modemap, "q", "quit");
    modemap->op->set_key(modemap, "j", "next-item");
    modemap->op->set_key(modemap, "k", "prev-item");
    modemap->op->set_key(modemap, "n", "next-page"); /* XXX: <space> */
    modemap->op->set_key(modemap, "p", "prev-page"); /* XXX: <b> */
    modemap->op->set_key(modemap, "h", "shift-left");
    modemap->op->set_key(modemap, "l", "shift-right");
    modemap->op->set_key(modemap, "m", "toggle-marked");
    modemap->op->set_key(modemap, "f", "modeswitch:filter");
    modemap->op->set_key(modemap, "/", "modeswitch:isearch");
    modemap->op->set_key(modemap, "s", "isearch-next"); /* XXX: <n> */
    modemap->op->set_vim_key(modemap, "<down>", "next-item");
    modemap->op->set_vim_key(modemap, "<tab>", "next-item");
    modemap->op->set_vim_key(modemap, "<up>", "prev-item");
    modemap->op->set_vim_key(modemap, "<s-tab>", "prev-item");
    modemap->op->set_vim_key(modemap, "<pagedown>", "next-page");
    modemap->op->set_vim_key(modemap, "<pageup>", "prev-page");
    modemap->op->set_vim_key(modemap, "<left>", "shift-left");
    modemap->op->set_vim_key(modemap, "<right>", "shift-right");
    modemap->op->set_vim_key(modemap, "<cr>", "accept");
    modemap->op->set_vim_key(modemap, "<esc>", "quit");
    modemap->op->set_vim_key(modemap, "<backspace>", "select-parent");

    for (i = 0; i < 2; i++)
    {
	if (i == 0) modemap = self->km_filter;
	else if (i == 1) modemap = self->km_search;
	modemap->op->clear_all_keys(modemap);
	modemap->op->set_vim_key(modemap, "<cr>", "accept");
	modemap->op->set_vim_key(modemap, "<tab>", "modeswitch:normal");
	modemap->op->set_vim_key(modemap, "<esc>", "modeswitch:normal");
	modemap->op->set_vim_key(modemap, "<backspace>", "input-bs");
	modemap->has_insert = 1;
    }

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

    init_DictIterator(&itkey);
    for (pkd = itkey.op->begin(&itkey, kmap); pkd != NULL; pkd = itkey.op->next(&itkey))
    {
	if (pkd->di_tv.v_type == VAR_STRING)
	{
	    modemap->op->set_vim_key(modemap, pkd->di_key, pkd->di_tv.vval.v_string);
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

    if (self->model)
	self->model->op->update_result(self->model, result);
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
_puls_update_hl_chain(_self)
    void *_self;
{
    METHOD(PopupList, update_hl_chain);
    ListHelper_T lst_chain;
    init_ListHelper(&lst_chain);
    lst_chain.first = (void**) &self->hl_chain;
    lst_chain.offs_next = offsetof(Highlighter_T, next);

    self->hl_chain = NULL;

    /* least important highlighter first */
    if (self->hl_user && self->hl_user->active)
	lst_chain.op->add_tail(&lst_chain, self->hl_user);

    if (self->hl_menu && self->hl_menu->active)
	lst_chain.op->add_tail(&lst_chain, self->hl_menu);

    lst_chain.op->add_tail(&lst_chain, self->hl_filter);
    lst_chain.op->add_tail(&lst_chain, self->hl_isearch);
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
    int		menu_mode;
    char_u	*text;
    int		item_count;
    LineHighlightWriter_T* lhwriter;
    LineWriter_T* writer;

    METHOD(PopupList, redraw);

    item_count = self->filter->op->get_item_count(self->filter);

    int hidden = item_count - self->position.height;
    int blank = 0;
    if ((self->first + self->position.height) > item_count)
	/* we have empty lines after the last item */
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

    self->op->update_hl_chain(self); /* TODO: update when really needed */
    lhwriter = new_LineHighlightWriter();
    lhwriter->highlighters = self->hl_chain;
    menu_mode = self->hl_menu && self->hl_menu->active;

    writer = (LineWriter_T*) lhwriter;
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

	if (self->model->op->has_flag(self->model, idx_model, ITEM_SEPARATOR))
	{
	    /* TODO: separator char setting; maybe as a part of border? */
	    screen_fill(row, row + 1, col, right+1, '-', '-', attr);
	}
	else
	{
	    if (self->model->op->has_flag(self->model, idx_model, ITEM_DISABLED))
	    {
		attr = (idx_filter == self->current)
		    ? _puls_hl_attrs[PULSATTR_DISABLED_SEL].attr
		    : _puls_hl_attrs[PULSATTR_DISABLED].attr;
	    }
	    else if (self->model->op->has_flag(self->model, idx_model, ITEM_MARKED))
	    {
		attr = (idx_filter == self->current) ? attr_select : attr_mark;
	    }

	    if (menu_mode)
	    {
		if (self->model->op->has_flag(self->model, idx_model, ITEM_DISABLED))
		    self->hl_menu->shortcut_attr = attr;
		else
		    self->hl_menu->shortcut_attr = (idx_filter == self->current)
			? _puls_hl_attrs[PULSATTR_SHORTCUT_SEL].attr
			: _puls_hl_attrs[PULSATTR_SHORTCUT].attr;
	    }

	    text = self->model->op->get_display_text(self->model, idx_model);
	    writer->op->write_line(writer, text, row, attr, ' ');
	}

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
_puls_on_filter_change(_self, data)
    void* _self;
    void* data;
{
    METHOD(PopupList, on_filter_change);
    self->filter->op->set_text(self->filter, self->line_edit->text);

    self->filter->op->filter_items(self->filter);
    if (self->current >= self->filter->op->get_item_count(self->filter))
	self->op->set_current(self, 0);
    self->need_redraw |= PULS_REDRAW_ALL;
    /* TODO: if (autosize) pplist->need_redraw |= PULS_REDRAW_RESIZE; */
    if (self->hl_filter)
	self->hl_filter->op->set_pattern(self->hl_filter, self->filter->text);
}

    static int
_puls_do_isearch(_self)
    void* _self;
{
    METHOD(PopupList, do_isearch);
    int item_count, start, end, step, i, idx_model;
    char_u* text;
    if (! self->filter || ! self->isearch)
	return 0;

    item_count = self->filter->op->get_item_count(self->filter);
    start = self->isearch->start;
    if (start < 0 || start >= item_count)
	start = 0;
    end = item_count;
    for (step = 0; step < 2; step++)
    {
	for(i = start; i < end; i++)
	{
	    idx_model = self->filter->op->get_model_index(self->filter, i);
	    text = self->model->op->get_display_text(self->model, idx_model);
	    if (self->isearch->op->match(self->isearch, text))
	    {
		step = 99; /* FOUND; used below */
		self->op->set_current(self, i);
		break;
	    }
	}
	/* XXX: wrap around on isearch ... could make it optional */
	end = start;
	start = 0;
    }

    if (step < 99)
    {
	self->op->set_current(self, self->isearch->start);
	return 0;
    }
    return 1;
}

    static int
_puls_on_isearch_change(_self, data)
    void* _self;
    void* data;
{
    METHOD(PopupList, on_isearch_change);
    self->isearch->op->set_text(self->isearch, self->line_edit->text);
    if (self->hl_isearch)
	self->hl_isearch->op->set_pattern(self->hl_isearch, self->isearch->text);

    self->op->do_isearch(self);
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
	int marked = !self->model->op->has_flag(self->model, idx_model_current, ITEM_MARKED);
	self->model->op->set_marked(self->model, idx_model_current, marked);
	self->need_redraw |= PULS_REDRAW_CURRENT;
	return 1;
    }
    else if (EQUALS(command, "input-bs")) {
	if (self->line_edit)
	{
	    if (self->line_edit->op->backspace(self->line_edit))
		self->line_edit->change_obsrvrs.op->notify(&self->line_edit->change_obsrvrs, NULL);
	}
	return 1;
    }
    else if (EQUALS(command, "isearch-next")) {
	if (self->isearch && *self->isearch->text)
	{
	    int cur = self->current;
	    self->isearch->start = self->current + 1;
	    if (! self->op->do_isearch(self))
		self->op->set_current(self, cur);
	}
	return 1;
    }
    else if (EQUALS(command, "select-parent"))
    {
	if (self->model)
	{
	    LOG(("Select parent"));
	    int rv = self->model->op->select_parent(self->model);
	    if (rv >= 0)
	    {
		/* reposition() has to be done before set_current */
		self->op->reposition(self);
		self->first = 0;
		self->op->set_current(self, rv);
		self->need_redraw |= PULS_REDRAW_ALL | PULS_REDRAW_CLEAR;
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
    char_u	bytes[MB_MAXBYTES + 1]; /* XXX: small, but remove from stack anyway */
    int		len;
    int		i;
#endif

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
	}
#ifdef FEAT_GUI
	else if (c == CSI)
	{
	    /* Translate a CSI to a CSI - KS_EXTRA - KE_CSI sequence */
	    *buf++ = CSI;
	    *buf++ = KS_EXTRA;
	    *buf++ = (int)KE_CSI;
	}
#endif
	else
	{
	    *buf++ = c;
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

    static void
_forced_redraw()
{
    /* from ex_redraw */
    int r = RedrawingDisabled;
    int p = p_lz;
    RedrawingDisabled = 0;
    p_lz = FALSE;
    update_topline();
    update_screen(CLEAR);
    RedrawingDisabled = r;
    p_lz = p;
    msg_didout = FALSE;
    msg_col = 0;
    out_flush();
}

    static int
_puls_test_loop(pplist, rettv)
    PopupList_T* pplist;
    typval_T*    rettv;
{
    char buf[32]; /* XXX: small, but remove from stack anyway */
    SimpleKeymap_T *modemap;
    ItemProvider_T *pmodel;
    WindowBorder_T *pborder;

#define MAX_KEY_SIZE	6*3+1		/* XXX: What is the longest sequence key_to_str can produce? */
#define MAX_SEQ_LEN	8*MAX_KEY_SIZE
    char_u sequence[MAX_SEQ_LEN];	/* current input sequence. XXX: remove from stack */
    char_u *ps;
    dictitem_T* seqmap;
    char_u* command;
    int seq_len, key, found, prev_found;

    pborder = pplist->border;
    pmodel = pplist->model;
    ps = pmodel->op->get_title(pmodel);
    if (ps != NULL)
	pplist->op->set_title(pplist, ps);

    if (pplist->current >= pmodel->op->get_item_count(pmodel))
	pplist->current = pmodel->op->get_item_count(pmodel) - 1;
    if (pplist->current < 0)
	pplist->current = 0;

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
	if (pplist->need_redraw & PULS_REDRAW_CLEAR)
	{
	    _forced_redraw();
	    pplist->op->redraw(pplist);
	}
	else if (pplist->need_redraw & PULS_REDRAW_RESIZE)
	{
	    _forced_redraw();
	    pplist->op->reposition(pplist);
	    pplist->op->redraw(pplist);
	}
	else if (pplist->need_redraw & PULS_REDRAW_ALL)
	{
	    pplist->op->redraw(pplist);
	}
	else if (pborder)
	{
	    pborder->op->draw_bottom(pborder);
	}
#if 0   /* XXX: Diagnostic code */
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
	    /* TODO: has_insert is just a quick solution; there may be other insertion points
	     * in the future, not just line_edit */
	    if (modemap->has_insert && prev_found != KM_PREFIX && !IS_SPECIAL(key))
	    {
		if (pplist->line_edit->op->add_text(pplist->line_edit, sequence))
		    pplist->line_edit->change_obsrvrs.op->notify(&pplist->line_edit->change_obsrvrs, NULL);
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

	/* TODO: find better names for actions
	 *    accept -> itemselected/itemactivated
	 *    quit   -> cancel
	 * TODO: change the names of the result returned from the PULS
	 *    accept -> ok         after itemclicked
	 *    quit   -> cancel     after escape
	 *    abort                when terminated with ctrl-c or similar
	 *    done                 the action was already executed by the provider
	 * */
	if (EQUALS(command, "quit"))
	    break;
	else if (EQUALS(command, "accept"))
	{
	    int cont;
	    int idx_model = pplist->filter->op->get_model_index(pplist->filter, pplist->current);

	    if (pmodel->op->has_flag(pmodel, idx_model, ITEM_DISABLED | ITEM_SEPARATOR))
		continue;

	    /* select_item(), cont:
	     *	 0 - let the caller handle it
	     *	 1 - remain in the event loop; update the items, they may have changed
	     *	-1 - exit the loop; return the result 'done' (executed by the handler)
	     */
	    cont = pmodel->op->select_item(pmodel, idx_model);
	    if (cont == 0)
		break;
	    else if (cont == 1)
	    {
		pplist->need_redraw |= PULS_REDRAW_ALL | PULS_REDRAW_RESIZE;
		pplist->op->set_current(pplist, 0);
		continue;
	    }
	    else
	    {
		/* TODO: change command to "done" */
		break;
	    }
	}
	else if (STARTSWITH(command, "accept:"))
	{
	    int cont;
	    int idx_model = pplist->filter->op->get_model_index(pplist->filter, pplist->current);

	    if (pmodel->op->has_flag(pmodel, idx_model, ITEM_DISABLED | ITEM_SEPARATOR))
		continue;

	    break;
	}
	else if (STARTSWITH(command, "done:"))
	{
	    break;
	}
	else if (EQUALS(command, "modeswitch:normal"))
	{
	    /* TODO: a function that will handle mode switches + events exit-mode, enter-mode */
	    pplist->line_edit->change_obsrvrs.op->remove_obj(&pplist->line_edit->change_obsrvrs, pplist);
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
		pplist->line_edit->max_len = MAX_FILTER_SIZE; /* TODO: set_max_len to trim the current value */
		pplist->line_edit->op->set_text(pplist->line_edit, pplist->filter->text);
		pborder->op->set_mode_text(pborder, "%");
		pborder->op->set_input_active(pborder, 1);
	    }
	    pplist->need_redraw |= PULS_REDRAW_FRAME;
	    pplist->line_edit->change_obsrvrs.op->add(&pplist->line_edit->change_obsrvrs,
		    pplist, _puls_on_filter_change);
	    continue;
	}
	else if (EQUALS(command, "modeswitch:isearch"))
	{
	    modemap = pplist->km_search;
	    if (pborder)
	    {
		pplist->line_edit->max_len = MAX_FILTER_SIZE; /* TODO: use set_max_len() to trim the current value */
		pplist->line_edit->op->set_text(pplist->line_edit, pplist->isearch->text);
		pborder->op->set_mode_text(pborder, "/");
		pborder->op->set_input_active(pborder, 1);
	    }
	    pplist->need_redraw |= PULS_REDRAW_FRAME;
	    pplist->isearch->start = pplist->current;
	    pplist->line_edit->change_obsrvrs.op->add(&pplist->line_edit->change_obsrvrs,
		    pplist, _puls_on_isearch_change);
	    continue;
	}
	else if (pplist->op->do_command(pplist, command))
	{
	    continue;
	}
	else
	{
	    char_u* next_command;
	    next_command = pmodel->op->handle_command(pmodel, pplist, command);
	}
    }

    /* TODO: consider that there could be multiple overlapping boxes */
    _forced_redraw();

    int	    rv = OK;
    dict_T  *d = dict_alloc();

    if (!d)
	rv = FAIL;
    else
    {
	rettv->vval.v_dict = d;
	rettv->v_type = VAR_DICT;
	++d->dv_refcount;

	dict_add_nr_str(d, "status", 0, command);

	if (EQUALS(command, "accept") || STARTSWITH(command, "accept:"))
	    pplist->op->prepare_result(pplist, d);
    }

    return rv;
}

/* 
 *    popuplist({items} [, {title} [, {options} ]])
 *
 *    Process the {items} in a popup window. {items} is a list or a string with 
 *    the name of an internal list (eg. buffers). An optional window title
 *    can be set with {title} or with an entry in the {options} dictionary.
 *    The following entries are supported:
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
 *
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
    char_u* title = NULL;
    list_T* items = NULL;
    dict_T* options = NULL;

    /*_init_vtables();*/
    _update_hl_attrs();
    init_DictIterator(&itd);

    LOG(("---------- New listbox -----------"));

    /* step 1: parse parameters; options dictionary is always last. */
    if (argvars[0].v_type == VAR_LIST)
	items = argvars[0].vval.v_list;
    else if (argvars[0].v_type == VAR_STRING)
	special_items = argvars[0].vval.v_string;
    else
    {
	return FAIL;
	/* TODO: errmsg param 0 must be LIST or STRING */
    }

    if (argvars[1].v_type != VAR_UNKNOWN)
    {
	if (argvars[1].v_type == VAR_STRING)
	    title = argvars[1].vval.v_string;
	else
	{
	    return FAIL;
	    /* TODO: errmsg param 1 must be STRING  */
	}

	if (argvars[2].v_type == VAR_DICT)
	    options = argvars[2].vval.v_dict;
	else if (argvars[2].v_type != VAR_UNKNOWN)
	{
	    return FAIL;
	    /* TODO: errmsg param 2 must be DICT  */
	}
    }

    /* step 2: create an item provider */
    if (!items && special_items)
    {
#ifdef FEAT_POPUPLIST_BUFFERS
	if (EQUALS(special_items, "buffers"))
	{
	    LOG(("Buffers"));
	    BufferItemProvider_T* bmodel = new_BufferItemProvider();
	    bmodel->op->list_buffers(bmodel);
	    model = (ItemProvider_T*) bmodel;
	}
#endif
/* TODO: #ifdef FEAT_POPUPLIST_MENU*/
	if (EQUALS(special_items, "menu") || EQUALS(special_items+1, "menu"))
	{
	    LOG(("Menu"));
	    MenuItemProvider_T* mmodel = new_MenuItemProvider();
	    if (!mmodel->op->parse_mode(mmodel, special_items))
	    {
		CLASS_DELETE(mmodel);
		/* TODO: errmsg: invalid menu mode '%special_items' */
		return FAIL;
	    }
	    mmodel->op->list_items(mmodel, NULL);
	    model = (ItemProvider_T*) mmodel;
	}
/*#endif*/
	if (EQUALS(special_items, "pulslog"))
	{
	    LOG(("PULS LOG"));
	    VimlistItemProvider_T* vlmodel = new_VimlistItemProvider();
	    vlmodel->op->set_list(vlmodel, &PULSLOG);
	    model = (ItemProvider_T*) vlmodel;
	}

	if (! model)
	{
	    /* TODO: errmsg: invalid item provider '%special_items' */
	    return FAIL;
	}
    }
    else if (items)
    {
	VimlistItemProvider_T* vlmodel = new_VimlistItemProvider();
	vlmodel->op->set_list(vlmodel, items);
	model = (ItemProvider_T*) vlmodel;
    }

    if (! model)
    {
	/* TODO: errmsg: no items defined */
	return FAIL;
    }

    /* step 3: pepare to execute and apply options */

    aligner = new_BoxAligner();
    aligner->op->set_limits(aligner, 0, 0, Columns - 1, Rows - 3);

    filter = new_ItemFilter();
    filter->model = model;

    pplist = new_PopupList();
    pplist->model = model;
    model->op->default_keymap(model, pplist);
    if (title)
       model->op->set_title(model, title);

    pplist->column_split = 1; /* TODO: option */

    pplist->filter  = filter;
    pplist->aligner = aligner;

    if (options)
    {
	pplist->op->read_options(pplist, options);

	option = dict_find(options, "commands", -1L);
	if (model && option && option->di_tv.v_type == VAR_DICT)
	{
	    /* XXX: for now, assume the options are const while the popup is running.
	     * Note that a callback could modify the options!
	     */
	    model->commands = option->di_tv.vval.v_dict;
	}

	model->op->read_options(model, options);
    }

    pplist->op->reposition(pplist);
    model->op->on_start(model);

    /* process the list */
    int rv = _puls_test_loop(pplist, rettv);

    CLASS_DELETE(pplist);
    CLASS_DELETE(aligner);
    CLASS_DELETE(filter);
    CLASS_DELETE(model);

    return rv;
}

#endif
