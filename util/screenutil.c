/* screenutil.c */

#include <curses.h>

void init_screen()
{
    initscr();
    cbreak();
    noecho();
    nonl();
    intrflush(stdscr, FALSE);
    keypad(stdscr, TRUE);
    scrollok(stdscr, TRUE);

    clear();
    refresh();
}


void close_screen()
{
    endwin();
}
