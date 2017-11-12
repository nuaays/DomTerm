#include "server.h"
#include <stdlib.h>

struct lws;

int is_domterm_action(int argc, char** argv, const char*cwd,
                      char **env, struct lws *wsi, int replyfd,
                      struct options *opts)
{
    return probe_domterm() > 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

int html_action(int argc, char** argv, const char*cwd,
                      char **env, struct lws *wsi, int replyfd,
                      struct options *opts)
{
    check_domterm(opts);
    int i = optind + 1;
    if (i == argc) {
        char buffer[1024];
        fprintf(stdout, "\033]72;");
        for (;;) {
            int r = fread(buffer, 1, sizeof(buffer), stdin);
            if (r <= 0 || fwrite(buffer, 1, r, stdout) <= 0)
                break;
        }
        fprintf(stdout, "\007");
    } else {
        while (i < argc)  {
            fprintf(stdout, "\033]72;%s\007", argv[i++]);
        }
    }
    fflush(stderr);
    return EXIT_SUCCESS;
}

char *read_response(FILE *err)
{
    int fin = get_tty_in();
    size_t bsize = 2048;
    char *buf = xmalloc(bsize);
    tty_save_set_raw(fin);
    int n = read(fin, buf, 2);
    char *msg = NULL;
    if (n != 2 ||
        (buf[0] != (char) 0x9D
         && (buf[0] != (char) 0xc2 || buf[1] != (char) 0x9d))) {
        msg = "(no response received)\n";
    } else {
        size_t old = 0;
        if (buf[0] == (char) 0x9d) {
            buf[0] = buf[1];
            old = 1;
        }
        for (;;) {
            ssize_t n = read(fin, buf+old, bsize-old);
            if (n <= 0) {
                msg = "(malformed response received)\n";
                break;
            }
            char *end = memchr(buf+old, '\n', n);
            if (end != NULL) {
                *end = '\0';
                break;
            }
            old += n;
            bsize = (3 * bsize) >> 1;
            buf = xrealloc(buf, bsize);
        }
    }
    if (msg) {
        fprintf(err, msg);
        free(buf);
        buf = NULL;
    }
    tty_restore(fin);
    return buf;
}

int print_stylesheet_action(int argc, char** argv, const char*cwd,
                            char **env, struct lws *wsi, int replyfd,
                            struct options *opts)
{
    check_domterm(opts);
    close(0);
    if (argc != 2) {
        char *msg = argc < 2 ? "(too few arguments to print-stylesheets)\n"
          : "(too many arguments to print-stylesheets)\n";
        write(replyfd, msg, strlen(msg)+1);
        close(replyfd);
        return EXIT_FAILURE;
    }
    FILE *out = fdopen(replyfd, "w");
    FILE *tout = fdopen(get_tty_out(), "w");
    fprintf(tout, "\033]93;%s\007", argv[1]); fflush(tout);
    char *response = read_response(out);
    json_object *jobj = json_tokener_parse(response);
    int nlines = json_object_array_length(jobj);
    for (int i = 0; i < nlines; i++)
        fprintf(stdout, "%s\n",
                json_object_get_string(json_object_array_get_idx(jobj, i)));
    free(response);
    json_object_put(jobj);
    return EXIT_SUCCESS;
}

int list_stylesheets_action(int argc, char** argv, const char*cwd,
                            char **env, struct lws *wsi, int replyfd,
                            struct options *opts)
{
    check_domterm(opts);
    close(0);
    write_to_tty("\033]90;\007", -1);
    FILE *out = fdopen(replyfd, "w");
    char *response = read_response(out);
    char *p = response;
    int i = 0;
    for (; *p != 0; ) {
      fprintf(stdout, "%d: ", i++);
      char *t = strchr(p, '\t');
      char *end = t != NULL ? t : p + strlen(p);
      fprintf(stdout, "%.*s\n", end-p, p);
      if (t == NULL)
        break;
      p = t+1;
    }
    return EXIT_SUCCESS;
}

int load_stylesheet_action(int argc, char** argv, const char*cwd,
                           char **env, struct lws *wsi, int replyfd,
                           struct options *opts)
{
    check_domterm(opts);
    if (argc != 3) {
        char *msg = argc < 3 ? "too few arguments to load-stylesheet\n"
          : "too many arguments to load-stylesheet\n";
        write(replyfd, msg, strlen(msg)+1);
        close(replyfd);
        return EXIT_FAILURE;
    }
    char *name = argv[1];
    char *fname = argv[2];
    int in = strcmp(fname, "-") == 0 ? 0 : open(fname, O_RDONLY);
    FILE *err = fdopen(replyfd, "w");
    if (in< 0) {
      fprintf(err, "cannot read '%s'\n", fname);
      return EXIT_FAILURE;
    }
    size_t bsize = 2048;
    int off = 0;
    char *buf = xmalloc(bsize);
    for (;;) {
        if (bsize == off) {
            bsize = (3 * bsize) >> 1;
            buf = xrealloc(buf, bsize);
        }
        ssize_t n = read(in, buf+off, bsize-off);
        if (n < 0) {
          // error
        }
        if (n <= 0)
           break;
        off += n;
    }
    FILE *tout = fdopen(get_tty_out(), "w");
    struct json_object *jname = json_object_new_string(name);
    struct json_object *jvalue = json_object_new_string_len(buf, off);
    fprintf(tout, "\033]95;%s,%s\007",
            json_object_to_json_string_ext(jname, JSON_C_TO_STRING_PLAIN),
            json_object_to_json_string_ext(jvalue, JSON_C_TO_STRING_PLAIN));
    json_object_put(jname);
    json_object_put(jvalue);

    fflush(tout);
    char *str = read_response(err);
    if (str != NULL && str[0]) {
        fprintf(err, "%s\n", str);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

int maybe_disable_stylesheet(bool disable, int argc, char** argv,
                             int replyfd, struct options *opts)
{
    check_domterm(opts);
    if (argc != 2) {
        char *msg = argc < 2 ? "(too few arguments to disable/enable-stylesheet)\n"
          : "(too many arguments to disable/enable-stylesheet)\n";
        write(replyfd, msg, strlen(msg)+1);
        close(replyfd);
        return EXIT_FAILURE;
    }
    char *specifier = argv[1];
    FILE *tout = fdopen(get_tty_out(), "w");
    fprintf(tout, "\033]%d;%s\007", disable?91:92, specifier);
    fflush(tout);
    FILE *out = fdopen(replyfd, "w");
    char *str = read_response(out);
    if (str != NULL && str[0]) {
        fprintf(out, "%s\n", str);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

int enable_stylesheet_action(int argc, char** argv, const char*cwd,
                             char **env, struct lws *wsi, int replyfd,
                             struct options *opts)
{
    return maybe_disable_stylesheet(false, argc, argv, replyfd, opts);
}

int disable_stylesheet_action(int argc, char** argv, const char*cwd,
                             char **env, struct lws *wsi, int replyfd,
                             struct options *opts)
{
    return maybe_disable_stylesheet(true, argc, argv, replyfd, opts);
}

int add_stylerule_action(int argc, char** argv, const char*cwd,
                            char **env, struct lws *wsi, int replyfd,
                            struct options *opts)
{
    check_domterm(opts);
    FILE *out = fdopen(get_tty_out(), "w");
    for (int i = 1; i < argc; i++) {
        struct json_object *jobj = json_object_new_string(argv[i]);
        fprintf(out, "\033]94;%s\007",
                json_object_to_json_string_ext(jobj, JSON_C_TO_STRING_PLAIN));
        fprintf(stderr, "add-style %s -> %s\n", argv[i], json_object_to_json_string_ext(jobj, JSON_C_TO_STRING_PLAIN));
        json_object_put(jobj);
    }
    fclose(out);
}

int reverse_video_action(int argc, char** argv, const char*cwd,
                         char **env, struct lws *wsi, int replyfd,
                         struct options *opts)
{
    check_domterm(opts);
    if (argc > 2) {
        char *msg ="too many arguments to reverse-video\n";
        write(replyfd, msg, strlen(msg)+1);
        close(replyfd);
        return EXIT_FAILURE;
    }
    char *opt = argc < 2 ? "on" : argv[1];
    bool on;
    if (strcasecmp(opt, "on") == 0 || strcasecmp(opt, "yes") == 0
        || strcasecmp(opt, "true") == 0)
      on = true;
    else if (strcasecmp(opt, "off") == 0 || strcasecmp(opt, "no") == 0
             || strcasecmp(opt, "false") == 0)
      on = false;
    else {
        char *msg ="arguments to reverse-video is not on/off/yes/no/true/false\n";
        write(replyfd, msg, strlen(msg)+1);
        close(replyfd);
        return EXIT_FAILURE;
    }
    char *cmd = on ? "\e[?5h" : "\e[?5l";
    write(get_tty_out(), cmd, strlen(cmd));
    return EXIT_SUCCESS;
}

struct command commands[] = {
  { .name = "is-domterm",
    .options = COMMAND_IN_CLIENT,
    .action = is_domterm_action },
  { .name ="html",
    .options = COMMAND_IN_CLIENT,
    .action = html_action },
  { .name ="add-style",
    .options = COMMAND_IN_CLIENT,
    .action = add_stylerule_action },
  { .name ="enable-stylesheet",
    .options = COMMAND_IN_CLIENT,
    .action = enable_stylesheet_action },
  { .name ="disable-stylesheet",
    .options = COMMAND_IN_CLIENT,
    .action = disable_stylesheet_action },
  { .name ="load-stylesheet",
    .options = COMMAND_IN_CLIENT,
    .action = load_stylesheet_action },
  { .name ="list-stylesheets",
    .options = COMMAND_IN_CLIENT,
    .action = list_stylesheets_action },
  { .name ="print-stylesheet",
    .options = COMMAND_IN_CLIENT,
    .action = print_stylesheet_action },
  { .name ="hcat",
    .options = COMMAND_IN_CLIENT|COMMAND_ALIAS },
  { .name = "attach", .options = COMMAND_IN_SERVER,
    .action = attach_action},
  { .name = "browse", .options = COMMAND_IN_SERVER,
    .action = browse_action},
  { .name = "list",
    .options = COMMAND_IN_CLIENT_IF_NO_SERVER|COMMAND_IN_SERVER,
    .action = list_action },
  { .name = "reverse-video",
    .options = COMMAND_IN_CLIENT,
    .action = reverse_video_action },
  { .name = "help",
    .options = COMMAND_IN_CLIENT,
    .action = help_action },
  { .name = "new", .options = COMMAND_IN_SERVER,
    .action = new_action},
  { .name = 0 }
  };

struct command *
find_command(const char *name)
{
    struct command *cmd = &commands[0];
    for (; ; cmd++) {
        if (cmd->name == NULL)
            return NULL;
        if (strcmp(cmd->name, name) == 0)
            break;
    }
    while ((cmd->options & COMMAND_ALIAS) != 0)
        cmd--;
    return cmd;
}
