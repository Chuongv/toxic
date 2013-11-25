/*
 * Toxic -- Tox Curses Client
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#include <tox/tox.h>

#include "chat.h"
#include "friendlist.h"
#include "misc_tools.h"

extern char *DATA_FILE;
extern ToxWindow *prompt;

static int max_friends_index = 0;    /* marks the index of the last friend in friends array */
static int num_friends = 0;
static int num_selected = 0;

static int friendlist_index[MAX_FRIENDS_NUM] = {0};

int index_name_cmp(const void *n1, const void *n2)
{
    int res = name_compare(friends[*(int *) n1].name, friends[*(int *) n2].name);

    int k = 100;
    /* Use weight to make qsort always put online friends before offline */
    res = friends[*(int *) n1].online ? (res - k) : (res + k);
    res = friends[*(int *) n2].online ? (res + k) : (res - k);

    return res;
}

/* sorts friendlist_index first by connection status then alphabetically */
void sort_friendlist_index(void)
{
    int i;
    int n = 0;

    for (i = 0; i < max_friends_index; ++i) {
        if (friends[i].active)
            friendlist_index[n++] = friends[i].num;
    }

    qsort(friendlist_index, num_friends, sizeof(int), index_name_cmp);
}

static void friendlist_onMessage(ToxWindow *self, Tox *m, int num, uint8_t *str, uint16_t len)
{
    if (num < 0 || num >= max_friends_index)
        return;

    if (friends[num].chatwin == -1)
        friends[num].chatwin = add_window(m, new_chat(m, friends[num].num));
}

static void friendlist_onConnectionChange(ToxWindow *self, Tox *m, int num, uint8_t status)
{
    if (num < 0 || num >= max_friends_index)
        return;

    friends[num].online = status == 1 ? true : false;
    sort_friendlist_index();
}

static void friendlist_onNickChange(ToxWindow *self, int num, uint8_t *str, uint16_t len)
{
    if (len >= TOX_MAX_NAME_LENGTH || num < 0 || num >= max_friends_index)
        return;

    str[TOXIC_MAX_NAME_LENGTH] = '\0';
    len = strlen(str) + 1;
    memcpy(friends[num].name, str, len);
    friends[num].namelength = len;
    sort_friendlist_index();
}

static void friendlist_onStatusChange(ToxWindow *self, Tox *m, int num, TOX_USERSTATUS status)
{
    if (num < 0 || num >= max_friends_index)
        return;

    friends[num].status = status;
}

static void friendlist_onStatusMessageChange(ToxWindow *self, int num, uint8_t *str, uint16_t len)
{
    if (len >= TOX_MAX_STATUSMESSAGE_LENGTH || num < 0 || num >= max_friends_index)
        return;

    memcpy(friends[num].statusmsg, str, len);
    friends[num].statusmsg_len = len;
}

static void friendlist_onFriendAdded(ToxWindow *self, Tox *m, int num)
{
    if (max_friends_index < 0 || max_friends_index >= MAX_FRIENDS_NUM)
        return;

    int i;

    for (i = 0; i <= max_friends_index; ++i) {
        if (!friends[i].active) {
            friends[i].num = num;
            friends[i].active = true;
            friends[i].chatwin = -1;
            friends[i].online = false;
            friends[i].status = TOX_USERSTATUS_NONE;
            friends[i].namelength = tox_getname(m, num, friends[i].name);
            memset(friends[i].pending_groupchat, 0, TOX_CLIENT_ID_SIZE);

            if (friends[i].namelength == -1 || friends[i].name[0] == '\0') {
                strcpy(friends[i].name, (uint8_t *) UNKNOWN_NAME);
                friends[i].namelength = strlen(UNKNOWN_NAME) + 1;
            } else {    /* Enforce toxic's maximum name length */
                friends[i].name[TOXIC_MAX_NAME_LENGTH] = '\0';
                friends[i].namelength = strlen(friends[i].name) + 1;
            }

            ++num_friends;

            if (i == max_friends_index)
                ++max_friends_index;

            sort_friendlist_index();
            return;
        }
    }
}

static void friendlist_onFileSendRequest(ToxWindow *self, Tox *m, int num, uint8_t filenum, 
                                         uint64_t filesize, uint8_t *filename, uint16_t filename_len)
{
    if (num < 0 || num >= max_friends_index)
        return;

    if (friends[num].chatwin == -1)
        friends[num].chatwin = add_window(m, new_chat(m, friends[num].num));
}

static void friendlist_onGroupInvite(ToxWindow *self, Tox *m, int num, uint8_t *group_pub_key)
{
    if (num < 0 || num >= max_friends_index)
        return;

    if (friends[num].chatwin == -1)
        friends[num].chatwin = add_window(m, new_chat(m, friends[num].num));
}

static void select_friend(Tox *m, wint_t key)
{
    if (key == KEY_UP) {
        if (--num_selected < 0)
            num_selected = num_friends - 1;
    } else if (key == KEY_DOWN) {
        num_selected = (num_selected + 1) % num_friends;
    }
}

static void delete_friend(Tox *m, ToxWindow *self, int f_num, wint_t key)
{
    tox_delfriend(m, f_num);
    memset(&friends[f_num], 0, sizeof(friend_t));
    
    int i;

    for (i = max_friends_index; i > 0; --i) {
        if (friends[i-1].active)
            break;
    }

    max_friends_index = i;
    --num_friends;

    /* make sure num_selected stays within num_friends range */
    if (num_friends && num_selected == num_friends)
        --num_selected;

    sort_friendlist_index();
    store_data(m, DATA_FILE);
}

static void friendlist_onKey(ToxWindow *self, Tox *m, wint_t key)
{
    if (num_friends == 0)
        return;

    int f = friendlist_index[num_selected];

    if (key == '\n') {
        /* Jump to chat window if already open */
        if (friends[f].chatwin != -1) {
            set_active_window(friends[f].chatwin);
        } else {
            friends[f].chatwin = add_window(m, new_chat(m, friends[f].num));
            set_active_window(friends[f].chatwin);
        }
    } else if (key == 0x107 || key == 0x8 || key == 0x7f) {
        delete_friend(m, self, f, key);
    } else {
        select_friend(m, key);
    }
}

static void friendlist_onDraw(ToxWindow *self, Tox *m)
{
    curs_set(0);
    werase(self->window);
    int x, y;
    getmaxyx(self->window, y, x);

    bool fix_statuses = x != self->x;    /* true if window x axis has changed */

    if (num_friends == 0) {
        wprintw(self->window, "Empty. Add some friends! :-)\n");
    } else {
        wattron(self->window, COLOR_PAIR(CYAN) | A_BOLD);
        wprintw(self->window, " Open chat with up/down keys and enter.\n");
        wprintw(self->window, " Delete friends with the backspace key.\n\n");
        wattroff(self->window, COLOR_PAIR(CYAN) | A_BOLD);
    }

    int i;

    for (i = 0; i < num_friends; ++i) {
        int f = friendlist_index[i];
        bool f_selected = false;

        if (friends[f].active) {
            if (i == num_selected) {
                wattron(self->window, A_BOLD);
                wprintw(self->window, " > ");
                wattroff(self->window, A_BOLD);
                f_selected = true;
            } else {
                wprintw(self->window, "   ");
            }
            
            if (friends[f].online) {
                TOX_USERSTATUS status = friends[f].status;
                int colour = WHITE;

                switch (status) {
                case TOX_USERSTATUS_NONE:
                    colour = GREEN;
                    break;
                case TOX_USERSTATUS_AWAY:
                    colour = YELLOW;
                    break;
                case TOX_USERSTATUS_BUSY:
                    colour = RED;
                    break;
                }

                wprintw(self->window, "[");
                wattron(self->window, COLOR_PAIR(colour) | A_BOLD);
                wprintw(self->window, "O");
                wattroff(self->window, COLOR_PAIR(colour) | A_BOLD);
                wprintw(self->window, "]");

                if (f_selected)
                    wattron(self->window, A_BOLD);

                wprintw(self->window, "%s", friends[f].name);

                if (f_selected)
                    wattroff(self->window, A_BOLD);

                /* Reset friends[f].statusmsg on window resize */
                if (fix_statuses) {
                    uint8_t statusmsg[TOX_MAX_STATUSMESSAGE_LENGTH] = {'\0'};
                    tox_copy_statusmessage(m, friends[f].num, statusmsg, TOX_MAX_STATUSMESSAGE_LENGTH);
                    snprintf(friends[f].statusmsg, sizeof(friends[f].statusmsg), "%s", statusmsg);
                    friends[f].statusmsg_len = tox_get_statusmessage_size(m, f);
                }

                /* Truncate note if it doesn't fit on one line */
                uint16_t maxlen = x - getcurx(self->window) - 4;
                if (friends[f].statusmsg_len > maxlen) {
                    friends[f].statusmsg[maxlen-3] = '\0';
                    strcat(friends[f].statusmsg, "...");
                    friends[f].statusmsg[maxlen] = '\0';
                    friends[f].statusmsg_len = maxlen;
                }

                wprintw(self->window, " (%s)\n", friends[f].statusmsg);
            } else {
                wprintw(self->window, "[O]");

                if (f_selected)
                    wattron(self->window, A_BOLD);

                wprintw(self->window, "%s\n", friends[f].name);

                if (f_selected)
                    wattroff(self->window, A_BOLD);
            }
        }
    }

    self->x = x;
    wrefresh(self->window);
}

void disable_chatwin(int f_num)
{
    friends[f_num].chatwin = -1;
}

static void friendlist_onInit(ToxWindow *self, Tox *m)
{

}

ToxWindow new_friendlist(void)
{
    ToxWindow ret;
    memset(&ret, 0, sizeof(ret));

    ret.onKey = &friendlist_onKey;
    ret.onDraw = &friendlist_onDraw;
    ret.onInit = &friendlist_onInit;
    ret.onFriendAdded = &friendlist_onFriendAdded;
    ret.onMessage = &friendlist_onMessage;
    ret.onConnectionChange = &friendlist_onConnectionChange;
    ret.onAction = &friendlist_onMessage;    // Action has identical behaviour to message
    ret.onNickChange = &friendlist_onNickChange;
    ret.onStatusChange = &friendlist_onStatusChange;
    ret.onStatusMessageChange = &friendlist_onStatusMessageChange;
    ret.onFileSendRequest = &friendlist_onFileSendRequest;
    ret.onGroupInvite = &friendlist_onGroupInvite;

    strcpy(ret.name, "friends");
    return ret;
}
