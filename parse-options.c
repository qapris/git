#include "git-compat-util.h"
#include "parse-options.h"

#define OPT_SHORT 1
#define OPT_UNSET 2

struct optparse_t {
	const char **argv;
	int argc;
	const char *opt;
};

static inline const char *get_arg(struct optparse_t *p)
{
	if (p->opt) {
		const char *res = p->opt;
		p->opt = NULL;
		return res;
	}
	p->argc--;
	return *++p->argv;
}

static inline const char *skip_prefix(const char *str, const char *prefix)
{
	size_t len = strlen(prefix);
	return strncmp(str, prefix, len) ? NULL : str + len;
}

static int opterror(const struct option *opt, const char *reason, int flags)
{
	if (flags & OPT_SHORT)
		return error("switch `%c' %s", opt->short_name, reason);
	if (flags & OPT_UNSET)
		return error("option `no-%s' %s", opt->long_name, reason);
	return error("option `%s' %s", opt->long_name, reason);
}

static int get_value(struct optparse_t *p,
                     const struct option *opt, int flags)
{
	const char *s, *arg;
	arg = p->opt ? p->opt : (p->argc > 1 ? p->argv[1] : NULL);

	if (p->opt && (flags & OPT_UNSET))
		return opterror(opt, "takes no value", flags);

	switch (opt->type) {
	case OPTION_BOOLEAN:
		if (!(flags & OPT_SHORT) && p->opt)
			return opterror(opt, "takes no value", flags);
		if (flags & OPT_UNSET)
			*(int *)opt->value = 0;
		else
			(*(int *)opt->value)++;
		return 0;

	case OPTION_STRING:
		if (flags & OPT_UNSET) {
			*(const char **)opt->value = (const char *)NULL;
			return 0;
		}
		if (opt->flags & PARSE_OPT_OPTARG && (!arg || *arg == '-')) {
			*(const char **)opt->value = (const char *)opt->defval;
			return 0;
		}
		if (!arg)
			return opterror(opt, "requires a value", flags);
		*(const char **)opt->value = get_arg(p);
		return 0;

	case OPTION_CALLBACK:
		if (flags & OPT_UNSET)
			return (*opt->callback)(opt, NULL, 1);
		if (opt->flags & PARSE_OPT_NOARG) {
			if (p->opt && !(flags & OPT_SHORT))
				return opterror(opt, "takes no value", flags);
			return (*opt->callback)(opt, NULL, 0);
		}
		if (opt->flags & PARSE_OPT_OPTARG && (!arg || *arg == '-'))
			return (*opt->callback)(opt, NULL, 0);
		if (!arg)
			return opterror(opt, "requires a value", flags);
		return (*opt->callback)(opt, get_arg(p), 0);

	case OPTION_INTEGER:
		if (flags & OPT_UNSET) {
			*(int *)opt->value = 0;
			return 0;
		}
		if (opt->flags & PARSE_OPT_OPTARG && (!arg || !isdigit(*arg))) {
			*(int *)opt->value = opt->defval;
			return 0;
		}
		if (!arg)
			return opterror(opt, "requires a value", flags);
		*(int *)opt->value = strtol(get_arg(p), (char **)&s, 10);
		if (*s)
			return opterror(opt, "expects a numerical value", flags);
		return 0;

	default:
		die("should not happen, someone must be hit on the forehead");
	}
}

static int parse_short_opt(struct optparse_t *p, const struct option *options)
{
	for (; options->type != OPTION_END; options++) {
		if (options->short_name == *p->opt) {
			p->opt = p->opt[1] ? p->opt + 1 : NULL;
			return get_value(p, options, OPT_SHORT);
		}
	}
	return error("unknown switch `%c'", *p->opt);
}

static int parse_long_opt(struct optparse_t *p, const char *arg,
                          const struct option *options)
{
	const char *arg_end = strchr(arg, '=');
	const struct option *abbrev_option = NULL;
	int abbrev_flags = 0;

	if (!arg_end)
		arg_end = arg + strlen(arg);

	for (; options->type != OPTION_END; options++) {
		const char *rest;
		int flags = 0;

		if (!options->long_name)
			continue;

		rest = skip_prefix(arg, options->long_name);
		if (!rest) {
			/* abbreviated? */
			if (!strncmp(options->long_name, arg, arg_end - arg)) {
is_abbreviated:
				if (abbrev_option)
					return error("Ambiguous option: %s "
						"(could be --%s%s or --%s%s)",
						arg,
						(flags & OPT_UNSET) ?
							"no-" : "",
						options->long_name,
						(abbrev_flags & OPT_UNSET) ?
							"no-" : "",
						abbrev_option->long_name);
				if (!(flags & OPT_UNSET) && *arg_end)
					p->opt = arg_end + 1;
				abbrev_option = options;
				abbrev_flags = flags;
				continue;
			}
			/* negated and abbreviated very much? */
			if (!prefixcmp("no-", arg)) {
				flags |= OPT_UNSET;
				goto is_abbreviated;
			}
			/* negated? */
			if (strncmp(arg, "no-", 3))
				continue;
			flags |= OPT_UNSET;
			rest = skip_prefix(arg + 3, options->long_name);
			/* abbreviated and negated? */
			if (!rest && !prefixcmp(options->long_name, arg + 3))
				goto is_abbreviated;
			if (!rest)
				continue;
		}
		if (*rest) {
			if (*rest != '=')
				continue;
			p->opt = rest + 1;
		}
		return get_value(p, options, flags);
	}
	if (abbrev_option)
		return get_value(p, abbrev_option, abbrev_flags);
	return error("unknown option `%s'", arg);
}

int parse_options(int argc, const char **argv, const struct option *options,
                  const char * const usagestr[], int flags)
{
	struct optparse_t args = { argv + 1, argc - 1, NULL };
	int j = 0;

	for (; args.argc; args.argc--, args.argv++) {
		const char *arg = args.argv[0];

		if (*arg != '-' || !arg[1]) {
			argv[j++] = args.argv[0];
			continue;
		}

		if (arg[1] != '-') {
			args.opt = arg + 1;
			do {
				if (*args.opt == 'h')
					usage_with_options(usagestr, options);
				if (parse_short_opt(&args, options) < 0)
					usage_with_options(usagestr, options);
			} while (args.opt);
			continue;
		}

		if (!arg[2]) { /* "--" */
			if (!(flags & PARSE_OPT_KEEP_DASHDASH)) {
				args.argc--;
				args.argv++;
			}
			break;
		}

		if (!strcmp(arg + 2, "help"))
			usage_with_options(usagestr, options);
		if (parse_long_opt(&args, arg + 2, options))
			usage_with_options(usagestr, options);
	}

	memmove(argv + j, args.argv, args.argc * sizeof(*argv));
	argv[j + args.argc] = NULL;
	return j + args.argc;
}

#define USAGE_OPTS_WIDTH 24
#define USAGE_GAP         2

void usage_with_options(const char * const *usagestr,
                        const struct option *opts)
{
	fprintf(stderr, "usage: %s\n", *usagestr++);
	while (*usagestr && **usagestr)
		fprintf(stderr, "   or: %s\n", *usagestr++);
	while (*usagestr)
		fprintf(stderr, "    %s\n", *usagestr++);

	if (opts->type != OPTION_GROUP)
		fputc('\n', stderr);

	for (; opts->type != OPTION_END; opts++) {
		size_t pos;
		int pad;

		if (opts->type == OPTION_GROUP) {
			fputc('\n', stderr);
			if (*opts->help)
				fprintf(stderr, "%s\n", opts->help);
			continue;
		}

		pos = fprintf(stderr, "    ");
		if (opts->short_name)
			pos += fprintf(stderr, "-%c", opts->short_name);
		if (opts->long_name && opts->short_name)
			pos += fprintf(stderr, ", ");
		if (opts->long_name)
			pos += fprintf(stderr, "--%s", opts->long_name);

		switch (opts->type) {
		case OPTION_INTEGER:
			if (opts->flags & PARSE_OPT_OPTARG)
				pos += fprintf(stderr, " [<n>]");
			else
				pos += fprintf(stderr, " <n>");
			break;
		case OPTION_CALLBACK:
			if (opts->flags & PARSE_OPT_NOARG)
				break;
			/* FALLTHROUGH */
		case OPTION_STRING:
			if (opts->argh) {
				if (opts->flags & PARSE_OPT_OPTARG)
					pos += fprintf(stderr, " [<%s>]", opts->argh);
				else
					pos += fprintf(stderr, " <%s>", opts->argh);
			} else {
				if (opts->flags & PARSE_OPT_OPTARG)
					pos += fprintf(stderr, " [...]");
				else
					pos += fprintf(stderr, " ...");
			}
			break;
		default:
			break;
		}

		if (pos <= USAGE_OPTS_WIDTH)
			pad = USAGE_OPTS_WIDTH - pos;
		else {
			fputc('\n', stderr);
			pad = USAGE_OPTS_WIDTH;
		}
		fprintf(stderr, "%*s%s\n", pad + USAGE_GAP, "", opts->help);
	}
	fputc('\n', stderr);

	exit(129);
}

/*----- some often used options -----*/
#include "cache.h"
int parse_opt_abbrev_cb(const struct option *opt, const char *arg, int unset)
{
	int v;

	if (!arg) {
		v = unset ? 0 : DEFAULT_ABBREV;
	} else {
		v = strtol(arg, (char **)&arg, 10);
		if (*arg)
			return opterror(opt, "expects a numerical value", 0);
		if (v && v < MINIMUM_ABBREV)
			v = MINIMUM_ABBREV;
		else if (v > 40)
			v = 40;
	}
	*(int *)(opt->value) = v;
	return 0;
}
