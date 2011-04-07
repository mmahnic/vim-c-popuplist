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
 * puls_pb.c: Buffer list provider for the Popup list (PULS)
 * NOTE: this file is included by popuplist.c
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

/* [ooc]
 *
  // the sort order is applied when the items are unfiltered
  const BUFSORT_NR	= 'n';
  const BUFSORT_NAME	= 'm';
  const BUFSORT_MRU	= 'r';
  const BUFSORT_EXT	= 'x';
  const BUFSORT_PATH	= 'p';
  class BufferItemProvider(ItemProvider) [bprov, variant FEAT_POPUPLIST_BUFFERS]
  {
    char	sorted_by;
    int		show_unlisted;
    void	init();
    void	destroy();
    void	read_options(dict_T* options);
    void	list_buffers();
    int		sort_buffers();
    char_u*	get_title();
    char_u*	handle_command(PopupList* puls, char_u* command, dict_T* status);
    void	default_keymap(PopupList* puls);
  };
*/

    static void
_bprov_init(_self)
    void* _self;
{
    METHOD(BufferItemProvider, init);
    self->sorted_by = BUFSORT_NR;
    self->show_unlisted = 0;
}

    static void
_bprov_destroy(_self)
    void* _self;
{
    METHOD(BufferItemProvider, destroy);
    END_DESTROY(BufferItemProvider);
}

    static char_u*
_bprov_get_title(_self)
    void* _self;
{
    METHOD(ItemProvider, get_title);
    static char title[] = "Buffers";
    return title;
}

    static void
_bprov_read_options(_self, options)
    void* _self;
    dict_T* options;
{
    METHOD(BufferItemProvider, read_options);
    dictitem_T* option;

    /* XXX: Options for options
     * The options can be provided by the user every time or they can be remembered
     * between runs.
     * To remember them we could have a static structure that would be created
     * the first time BufferItemProvider is executed.
     * The other possibility is to let the user handle the options for each use-case
     * in vimscript; in this case we pass the command 'option-changed' to the
     * script after an option is changed.
     */
    option = dict_find(options, "unlisted", -1L);
    if (option && option->di_tv.v_type == VAR_NUMBER)
    {
	self->show_unlisted = option->di_tv.vval.v_number ? 1 : 0;
    }

    option = dict_find(options, "sort", -1L);
    if (option && option->di_tv.v_type == VAR_STRING)
    {
	self->sorted_by = *option->di_tv.vval.v_string;
    }
}


    static void
_bprov_list_buffers(_self)
    void* _self;
{
    METHOD(BufferItemProvider, list_buffers);
    buf_T	*buf;
    int		len;
    int		i;
    char_u	*fname;
    char_u	*dirname;
    char	curdir[] = ".";
    PopupItem_T* pit;

    self->op->clear_items(self);

    for (buf = firstbuf; buf != NULL && !got_int; buf = buf->b_next)
    {
	/* skip unlisted buffers */
	if (! self->show_unlisted && ! buf->b_p_bl)
	    continue;

	if (buf_spname(buf) != NULL)
	{
	    STRCPY(NameBuff, buf_spname(buf));
	    fname = NameBuff;
	    dirname = curdir;
	}
	else
	{
	    /* XXX: modify_fname, home_replace, shorten_fname, mch_dirname
	     * ... can't figure out a good solution, so we split by '/'
	     */
	    home_replace(buf, buf->b_fname, NameBuff, MAXPATHL, TRUE);
	    fname = vim_strrchr(NameBuff, '/');
	    if (fname)
	    {
		*fname = NUL; /* truncate dirname */
		fname++;
		dirname = NameBuff;
	    }
	    else
	    {
		fname = NameBuff;
		dirname = curdir;
	    }
	}

	len = vim_snprintf((char *)IObuff, IOSIZE - 20, "%3d%c%c%c%c%c %s\t%s",
		buf->b_fnum,
		buf->b_p_bl ? ' ' : 'u',
		buf == curbuf ? '%' :
			(curwin->w_alt_fnum == buf->b_fnum ? '#' : ' '),
		buf->b_ml.ml_mfp == NULL ? ' ' :
			(buf->b_nwindows == 0 ? 'h' : 'a'),
		!buf->b_p_ma ? '-' : (buf->b_p_ro ? '=' : ' '),
		(buf->b_flags & BF_READERR) ? 'x'
					    : (bufIsChanged(buf) ? '+' : ' '),
		fname,
		dirname);

	pit = self->op->append_pchar_item(self, vim_strsave(IObuff), !ITEM_SHARED);
	if (pit)
	{
	    pit->data = (void *)buf;
	    pit->filter_start = 9;

	    i = 1000;
	    while (buf->b_fnum >= i)
	    {
		++pit->filter_start;
		i *= 10;
	    }
	}
    }
}

   static int
_BufferItem_cmp_nr(a, b) /* PopupItemCompare_Fn */
    PopupItem_T* a;
    PopupItem_T* b;
{
    if ( ((buf_T*)a->data)->b_fnum < ((buf_T*)b->data)->b_fnum )
	return LT_SORT_UP;
    if ( ((buf_T*)a->data)->b_fnum > ((buf_T*)b->data)->b_fnum )
	return GT_SORT_UP;
    return 0;
}

   static int
_BufferItem_cmp_path(a, b) /* PopupItemCompare_Fn */
    PopupItem_T* a;
    PopupItem_T* b;
{
    return STRCMP_SORT_UP * STRCMP( ((buf_T*)a->data)->b_ffname, ((buf_T*)b->data)->b_ffname );
}

    static int
_bprov_sort_buffers(_self)
    void* _self;
{
    METHOD(BufferItemProvider, sort_buffers);

    switch (self->sorted_by)
    {
	case BUFSORT_NR:
	    return self->op->sort_items(self, _BufferItem_cmp_nr);
	case BUFSORT_PATH:
	    return self->op->sort_items(self, _BufferItem_cmp_path);
	case BUFSORT_NAME:
	    return self->op->sort_items(self, _BufferItem_cmp_path); /* TODO: _name */
	case BUFSORT_EXT:
	    return self->op->sort_items(self, _BufferItem_cmp_path); /* TODO: _ext */
	case BUFSORT_MRU:
	    return self->op->sort_items(self, _BufferItem_cmp_nr); /* TODO: _mru */
    }

    /* unknown sort mode -> sort by number */
    self->sorted_by = BUFSORT_NR;
    return self->op->sort_items(self, _BufferItem_cmp_nr);
}

    static char_u*
_bprov_handle_command(_self, puls, command, status)
    void*	    _self;
    PopupList_T*    puls;
    char_u*	    command;
    dict_T*	    status;
{
    METHOD(BufferItemProvider, handle_command);

    if (STARTSWITH(command, "buf-sort"))
    {
	int modified = 0;
	int mode = NUL;
	if (EQUALS(command, "buf-sort-bufnr"))
	    mode = BUFSORT_NR;
	else if (EQUALS(command, "buf-sort-path"))
	    mode = BUFSORT_PATH;
	else if (EQUALS(command, "buf-sort-name"))
	    mode = BUFSORT_NAME;
	else if (EQUALS(command, "buf-sort-ext"))
	    mode = BUFSORT_EXT;
	else if (EQUALS(command, "buf-sort-mru"))
	    mode = BUFSORT_MRU;

	if (mode != NUL)
	{
	    self->sorted_by = mode;
	    modified = self->op->sort_buffers(self);
	    if (modified)
		puls->need_redraw |= PULS_REDRAW_ALL;
	}
    }
    else if (EQUALS(command, "buf-delete") || EQUALS(command, "buf-wipeout"))
    {
	char cbuf[32];
	PopupItem_T* pit = self->op->get_item(self, puls->current);
	if (pit)
	{
	    buf_T* bi = (buf_T*) pit->data;
	    sprintf(cbuf, "b%s %d", command+4, bi->b_fnum);
	    do_cmdline_cmd(cbuf);
	}
	
	self->op->list_buffers(self); /* TODO: remove the buffer from item list instead of recreating! */
	self->op->sort_buffers(self); /* TODO: if filter is active, sorting should be disabled */
	puls->need_redraw |= PULS_REDRAW_ALL;
    }
    else if (EQUALS(command, "buf-toggle-unlisted"))
    {
	self->show_unlisted = ! self->show_unlisted;
	self->op->list_buffers(self); /* TODO: remove the buffer from item list instead of recreating! */
	self->op->sort_buffers(self); /* TODO: if filter is active, sorting should be disabled */
	puls->need_redraw |= PULS_REDRAW_ALL;
    }
    else
    {
	return super(BufferItemProvider, handle_command)(self, puls, command, status);
    }

    return NULL;
}

    static void
_bprov_default_keymap(_self, puls)
    void* _self;
    PopupList_T* puls;
{
    METHOD(ItemProvider, default_keymap);
    SimpleKeymap_T* modemap;

    modemap = puls->km_normal;
    modemap->op->set_key(modemap, "xd", "buf-delete");
    modemap->op->set_key(modemap, "xw", "buf-wipeout");
    modemap->op->set_key(modemap, "ob", "buf-sort-bufnr");
    modemap->op->set_key(modemap, "on", "buf-sort-name");
    modemap->op->set_key(modemap, "op", "buf-sort-path");
    modemap->op->set_key(modemap, "or", "buf-sort-mru");
    modemap->op->set_key(modemap, "ox", "buf-sort-ext");
    modemap->op->set_key(modemap, "u",  "buf-toggle-unlisted");

    super(BufferItemProvider, default_keymap)(self, puls);
}
