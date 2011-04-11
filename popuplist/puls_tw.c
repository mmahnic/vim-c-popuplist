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
 * puls_tw.c: Text writer for the Popup list (PULS)
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
  // Write text in one line. Take care of line offsets. Expand tabs.
  const PLWR_MAX_FIXED_TABS = 8;
  class LineWriter [plwr]
  {
    int	    fixed_tabs[PLWR_MAX_FIXED_TABS+1];	// predefined tab positions; last is 0
    int	    tab_size;	// tab size after the last position; negative = fixed number of spaces
    int	    offset;	// number of cells the text is shifted to the left
    int	    min_col;	// start column
    int	    max_col;	// end column
    void    init();
    void    destroy();
    void    add_fixed_tab(int col);
    int	    get_tab_size_at(int col);
    void    set_limits(int min_col, int max_col);
    // write the text and fill to max_col with fillChar if it is not NUL
    int	    write_line(char_u* text, int attr, int row, int fillChar);
  };
*/

    static void
_plwr_init(_self)
    void* _self;
{
    METHOD(LineWriter, init);
    self->fixed_tabs[0] = 0;
    self->tab_size = 8;
    self->min_col = 0;
    self->max_col = Columns-1;
    self->offset = 0;
}

    static void
_plwr_destroy(_self)
    void* _self;
{
    METHOD(LineWriter, destroy);
    END_DESTROY(LineWriter);
}

    static int
_int_compare (a, b)
    const void* a;
    const void* b;
{
    return ( *(int*)a - *(int*)b );
}

    static void
_plwr_add_fixed_tab(_self, col)
    void* _self;
    int col;
{
    METHOD(LineWriter, add_fixed_tab);
    int i = 0;
    while (i < PLWR_MAX_FIXED_TABS && self->fixed_tabs[i] > 0)
	i++;
    if (i < PLWR_MAX_FIXED_TABS)
    {
	self->fixed_tabs[i] = col;
	self->fixed_tabs[i+1] = 0;
	if (i > 0)
	    qsort(self->fixed_tabs, i+1, sizeof(int), &_int_compare);
    }
}

    static int
_plwr_get_tab_size_at(_self, col)
    void* _self;
    int col;
{
    METHOD(LineWriter, get_tab_size_at);
    int i = 0;
    int last_tab = 0;
    if (self->fixed_tabs[0] > 0)
    {
	while(self->fixed_tabs[i] > 0 && self->fixed_tabs[i] <= col)
	    i++;
	if (self->fixed_tabs[i] > col)
	    return (self->fixed_tabs[i] - col);
	else
	    last_tab = self->fixed_tabs[i-1];
    }
    if (self->tab_size < 0)
	return -self->tab_size;
    if (self->tab_size == 0)
	return 2;
    return self->tab_size - ((col - last_tab) % self->tab_size);
}

    static void
_plwr_set_limits(_self, min_col, max_col)
    void* _self;
    int min_col;
    int max_col;
{
    METHOD(LineWriter, set_limits);
    self->min_col = min_col;
    self->max_col = max_col;
}

/*
 *        L         R		Border
 *        01234567890		Box column
 * 012345678901234567890	Text column
 * .......7.............	Offset = 7
 *        .         .
 * ABCDEFGHI ABCDEFGHI A	Single characters in text
 * 123456789012345678		... text width (pwidth)
 *        HI ABCDEFGH		... displayed
 *        .         .
 * ABCDDDEEE ABCDDDEEE A	Wide characters in text
 * 1236669990123666999		... text width (pwidth)
 *        .. ABCDDD..		... displayed
 */
    static int
_plwr_write_line(_self, text, row, attr, fillChar)
    void* _self;
    char_u* text;
    int row;
    int attr;
    int fillChar;
{
    METHOD(LineWriter, write_line);
    char_u *p, *s;
    int pwidth, w, col, endcol;

    if (!text)
	text = blankline;
    p = text;		    /* current character */
    s = NULL;		    /* string start */
    pwidth = 0;		    /* total width including current character */
    col = self->min_col;    /* write position */

    for ( ; p != NULL; mb_ptr_adv(p))
    {
	if (*p == TAB)
	    w = self->op->get_tab_size_at(self, pwidth);
	else
	    w = ptr2cells(p);
	pwidth += w;
	if (pwidth <= self->offset)
	{
	    if (*p == NUL) break;
	    else continue;
	}
	if (s == NULL)
	{
	    if (pwidth - self->offset < w)
	    {
		/* character partly visible -- output spaces */
		endcol = col + pwidth - 1 - self->offset;
		screen_fill(row, row + 1, col, endcol + 1, ' ', ' ', attr);
		col = endcol + 1;
		continue;
	    }
	    s = p;
	}
	if (*p == NUL || *p == TAB || pwidth >= self->max_col)
	{
	    /* Display the text that fits or comes before a Tab.
	     * First convert it to printable characters. */
	    char_u	*st;
	    int	saved = *p;

	    *p = NUL;
	    st = transstr(s);
	    *p = saved;
	    if (st)
	    {
		screen_puts_len(st, (int)STRLEN(st), row, col, attr);
		col += vim_strsize(st);
		vim_free(st);
	    }
	    s = NULL;
	    if (*p == TAB)
	    {
		endcol = limit_value(col + w - 1, col, self->max_col);
		screen_fill(row, row + 1, col, endcol + 1, ' ', ' ', attr);
		col = endcol + 1;
	    }
	    else break;
	}
    }
    if (col <= self->max_col && fillChar != NUL)
	screen_fill(row, row + 1, col, self->max_col+1, fillChar, fillChar, attr);
}
