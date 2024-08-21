#include <ncurses.h>

int main(void) {
  initscr();

  cbreak();
  noecho();

  refresh();

  WINDOW* win1 = newwin(10, 10, 20, 5);
  // box(win1, '|', ' -');
  wborder(win1, '|', '|', '-', '-', '+', '+', '+', '+');

  wprintw(win1, "Hello World! What?");
  // mvwprintw(win1, 1, 1, "Hello World and other things.");

  wrefresh(win1);

  getch();

  delwin(win1);
  endwin();

  return 0;
}
