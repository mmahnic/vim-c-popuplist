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
 * puls_pm.c: Provider that adds Vim menu items to the Popup list (PULS).
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
  // TODO: , variant FEAT_POPUPLIST_MENU]
  class MenuItemProvider(ItemProvider) [mnupr]
  {
    vimmenu_T*  top_menu;  // the initial menu to be displayed; gui_find_menu
    vimmenu_T*  cur_menu;  // the currently displayed menu
    int		mode;      // the mode where the menu is executed
    void	init();
    void	destroy();
    int		parse_mode(char_u* command);
    int		list_items(void* selected);
    int		select_item(int item);
    int		select_parent();
    // void	read_options(dict_T* options);
    // void	default_keymap(PopupList* puls);
  };
*/

static char g_separator[] = "-";

    static void
_mnupr_init(_self)
    void* _self;
{
    METHOD(MenuItemProvider, init);
    self->top_menu = root_menu;
    self->cur_menu = self->top_menu;
    self->mode = MENU_NORMAL_MODE;
}

    static void
_mnupr_destroy(_self)
    void* _self;
{
    METHOD(MenuItemProvider, destroy);
    END_DESTROY(MenuItemProvider);
}

    static int
_mnupr_parse_mode(_self, command)
    void* _self;
    char_u* command;
{
    METHOD(MenuItemProvider, parse_mode);
    if (EQUALS(command, "menu"))
    {
	self->mode = MENU_NORMAL_MODE;
	return 1;
    }
    /* based on get_menu_cmd_modes */
    switch(command[0])
    {
	case 'v':
	    self->mode = MENU_VISUAL_MODE;
	    return 1;
	case 's':
	    self->mode = MENU_SELECT_MODE;
	    return 1;
	case 'o':
	    self->mode = MENU_OP_PENDING_MODE;
	    return 1;
	case 'i':
	    self->mode = MENU_INSERT_MODE;
	    return 1;
	case 't':
	    self->mode = MENU_TIP_MODE;
	    return 1;
	case 'c':
	    self->mode = MENU_CMDLINE_MODE;
	    return 1;
	case 'n':
	    self->mode = MENU_NORMAL_MODE;
	    return 1;
    }
    return 0;
}

    static int
_mnupr_list_items(_self, selected)
    void* _self;
    void* selected;
{
    METHOD(MenuItemProvider, list_items);
    vimmenu_T* pm;
    PopupItem_T* pit;
    int len, isel, i;
    int mode = self->mode;
    self->op->clear_items(self);

    /*LOG(("Menu s:%04x m:%04x %s", State, mode, self->cur_menu->name));*/

    isel = -1;
    i = 0;
    for (pm = self->cur_menu; pm != NULL; pm = pm->next)
    {
	/*LOG(("  m:%04x e:%04x %s", pm->modes, pm->enabled, pm->name));*/
	if (menu_is_separator(pm->name))
	{
	    pit = self->op->append_pchar_item(self, g_separator, ITEM_SHARED);
	    if (pit)
		pit->flags |= ITEM_SEPARATOR;
	}
	else
	{
	    /* TODO: Various Popup menus should be skipped. */
	    len = vim_snprintf((char *)IObuff, IOSIZE - 20, "%c %s\t%s",
		    pm->children ? '+' : ' ', /* TODO: submenu-icon setting */
		    pm->name,
		    pm->actext ? (char*)pm->actext : "");
	    pit = self->op->append_pchar_item(self, vim_strsave(IObuff), !ITEM_SHARED);
	    if (pit)
	    {
		pit->data = (void*)pm;
		if (!(pm->modes & mode) || !(pm->enabled & mode))
		{
		    pit->flags |= ITEM_DISABLED;
		}

		if (pm == selected && isel < 0)
		    isel = i;
		i++;
	    }
	}
    }

    return isel;
}

    static int
_mnupr_select_item(_self, item)
    void* _self;
    int item;
{
    METHOD(MenuItemProvider, select_item);
    PopupItem_T* pit;
    vimmenu_T* pm;
    pit = self->op->get_item(self, item);
    if (!pit || !pit->data)
	return 1; /* continue event loop because the selected item is invalid */

    pm = (vimmenu_T*) pit->data;
    if (pm->children)
    {
	/* When a submenu is selected we could create a new popuplist and start
	 * a new event loop. When the child puls would finish, we would return 
	 * either 1 to continue (show parent), or -1 if the child already executed
	 * a command. If the child puls would be cancelled, we would have to return
	 * another value (eg. -2). The main loop would also have to redraw the whole
	 * window stack (when we return 1) which is not implemented, yet.
	 * ATM we replace the contents of the current puls and continue. */
	self->cur_menu = pm->children;
	self->op->list_items(self, NULL);
	return 1;
    }
    else
    {
	int mode = self->mode;
	/* TODO: More has to be done for menus in visual mode.
	 * By default the menu is shown for every selected line. */
	gui_menu_cb(pm);
	return -1;
    }
}

    static int
_mnupr_select_parent(_self)
    void* _self;
{
    METHOD(MenuItemProvider, select_parent);
    vimmenu_T *cur, *parent;
    int icur;

    if (!self->cur_menu || !self->cur_menu->parent)
	return -1; /* no parent, ignore */
    if (self->cur_menu == self->top_menu)
	return -1; /* not allowed to go beyond the parent */

    parent = self->cur_menu->parent;
    cur = parent; /* we want to highlight this one in the parent menu */
    if (parent->parent)
	parent = parent->parent->children; /* the first menu entry in the parent menu */
    else
	parent = self->top_menu;
    self->cur_menu = parent;
    icur = self->op->list_items(self, cur);
    if (icur < 0)
	icur = 0;

    return icur;
}

