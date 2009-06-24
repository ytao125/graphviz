/* $Id$ $Revision$ */
/* vim:set shiftwidth=4 ts=8: */

/**********************************************************
*      This software is part of the graphviz package      *
*                http://www.graphviz.org/                 *
*                                                         *
*            Copyright (c) 1994-2004 AT&T Corp.           *
*                and is licensed under the                *
*            Common Public License, Version 1.0           *
*                      by AT&T Corp.                      *
*                                                         *
*        Information and Software Systems Research        *
*              AT&T Research, Florham Park NJ             *
**********************************************************/


/*
 * gpr: graph pattern recognizer
 *
 * Written by Emden Gansner
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#else
#include "compat_unistd.h"
#endif
#include "builddate.h"
#include "gprstate.h"
#include "cgraph.h"
#include "ingraphs.h"
#include "compile.h"
#include "queue.h"
#include "gvpr.h"
#include "actions.h"
#include "sfstr.h"
#include <error.h>
#include <string.h>
#include <ctype.h>
#include <setjmp.h>

#define DFLT_GPRPATH    "."

#define GV_USE_JUMP 4

static char *Info[] = {
    "gvpr",                     /* Program */
    VERSION,                    /* Version */
    BUILDDATE                   /* Build Date */
};

static const char *usage =
    " [-o <ofile>] [-a <args>] ([-f <prog>] | 'prog') [files]\n\
   -c         - use source graph for output\n\
   -f <pfile> - find program in file <pfile>\n\
   -i         - create node induced subgraph\n\
   -a <args>  - string arguments available as ARGV[0..]\n\
   -o <ofile> - write output to <ofile>; stdout by default\n\
   -q         - turn off warning messages\n\
   -V         - print version info\n\
   -?         - print usage info\n\
If no files are specified, stdin is used\n";

typedef struct {
    char *cmdName;              /* command name */
    Sfio_t *outFile;		/* output stream; stdout default */
    char *program;              /* program source */
    int useFile;		/* true if program comes from a file */
    int compflags;
    char **inFiles;
    int argc;
    char **argv;
    int state;                  /* > 0 : continue; <= 0 finish */
} options;

static Sfio_t *openOut(char *name)
{
    Sfio_t *outs;

    outs = sfopen(0, name, "w");
    if (outs == 0) {
	error(ERROR_ERROR, "could not open %s for writing", name);
    }
    return outs;
}

/* gettok:
 * Tokenize a string. Tokens consist of either a non-empty string
 * of non-space characters, or all characters between a pair of
 * single or double quotes. As usual, we map 
 *   \c -> c
 * for all c
 * Return next argument token, returning NULL if none.
 * sp is updated to point to next character to be processed.
 * NB. There must be white space between tokens. Otherwise, they
 * are concatenated.
 */
static char *gettok(char **sp)
{
    char *s = *sp;
    char *ws = s;
    char *rs = s;
    char c;
    char q = '\0';		/* if non-0, in quote mode with quote char q */

    while (isspace(*rs))
	rs++;
    if ((c = *rs) == '\0')
	return NULL;
    while ((c = *rs)) {
	if (q && (q == c)) {	/* end quote */
	    q = '\0';
	} else if (!q && ((c == '"') || (c == '\''))) {
	    q = c;
	} else if (c == '\\') {
	    rs++;
	    c = *rs;
	    if (c)
		*ws++ = c;
	    else {
		error(ERROR_WARNING,
		      "backslash in argument followed by no character - ignored");
		rs--;
	    }
	} else if (q || !isspace(c))
	    *ws++ = c;
	else
	    break;
	rs++;
    }
    if (*rs)
	rs++;
    else if (q)
	error(ERROR_WARNING, "no closing quote for argument %s", s);
    *sp = rs;
    *ws = '\0';
    return s;
}

#define NUM_ARGS 100

/* parseArgs:
 * Split s into whitespace separated tokens, allowing quotes.
 * Append tokens to argument list and return new number of arguments.
 * argc is the current number of arguments, with the arguments
 * stored in *argv.
 */
static int parseArgs(char *s, int argc, char ***argv)
{
    int i, cnt = 0;
    char *args[NUM_ARGS];
    char *t;
    char **av;

    while ((t = gettok(&s))) {
	if (cnt == NUM_ARGS) {
	    error(ERROR_WARNING,
		  "at most %d arguments allowed per -a flag - ignoring rest",
		  NUM_ARGS);
	    break;
	}
	args[cnt++] = t;
    }

    if (cnt) {
	int oldcnt = argc;
	argc = oldcnt + cnt;
	av = oldof(*argv, char *, argc, 0);
	for (i = 0; i < cnt; i++)
	    av[oldcnt + i] = strdup(args[i]);
	*argv = av;
    }
    return argc;
}


#ifdef WIN32
#define PATHSEP '\\'
#define LISTSEP ';'
#else
#define PATHSEP '/'
#define LISTSEP ':'
#endif

/* resolve:
 * Translate -f arg parameter into a pathname.
 * If arg contains '/', return arg.
 * Else search directories in GPRPATH for arg.
 * Return NULL on error.
 */
static char *resolve(char *arg)
{
    char *path;
    char *s;
    char *cp;
    char *fname = 0;
    Sfio_t *fp;
    size_t sz;

#ifdef WIN32_DLL
    if (!pathisrelative(arg))
#else
    if (strchr(arg, '/'))
#endif
	return strdup(arg);

    path = getenv("GPRPATH");
    if (!path)
	path = DFLT_GPRPATH;

    if (!(fp = sfstropen())) {
	error(ERROR_ERROR, "Could not open buffer");
	return 0;
    }

    while (*path && !fname) {
	if (*path == LISTSEP) {	/* skip colons */
	    path++;
	    continue;
	}
	cp = strchr(path, LISTSEP);
	if (cp) {
	    sz = (size_t) (cp - path);
	    sfwrite(fp, path, sz);
	    path = cp + 1;	/* skip past current colon */
	} else {
	    sz = sfprintf(fp, path);
	    path += sz;
	}
	sfputc(fp, PATHSEP);
	sfprintf(fp, arg);
	s = sfstruse(fp);

	if (access(s, R_OK) == 0) {
	    fname = strdup(s);
	}
    }

    if (!fname)
	error(ERROR_ERROR, "Could not find file \"%s\" in GPRPATH", arg);

    sfclose(fp);
    return fname;
}

static char*
getOptarg (int c, char** argp, int* argip, int argc, char** argv)
{
    char* rv; 
    char* arg = *argp;
    int argi = *argip;

    if (*arg) {
	rv = arg;
	while (*arg) arg++; 
	*argp = arg;
    }
    else if (argi < argc) {
	rv = argv[argi++];
	*argip = argi;
    } 
    else {
	rv = NULL;
	error(ERROR_WARNING, "missing argument for option -%c", c);
    }
    return rv;
}

/* doFlags:
 * Process a command-line argument starting with a '-'.
 * argi is the index of the next available item in argv[].
 * argc has its usual meaning.
 *
 * return > 0 given next argi value
 *        = 0 for exit with 0
 *        < 0 for error
 */
static int
doFlags(char* arg, int argi, int argc, char** argv, options* opts)
{
    int c;

    while ((c = *arg++)) {
	switch (c) {
	case 'c':
	    opts->compflags |= SRCOUT;
	    break;
	case 'C':
	    opts->compflags |= (SRCOUT|CLONE);
	    break;
	case 'f':
	    if ((optarg = getOptarg(c, &arg, &argi, argc, argv)) && (opts->program = resolve(optarg))) {
		opts->useFile = 1;
	    }
	    else return -1;
	    break;
	case 'i':
	    opts->compflags |= INDUCE;
	    break;
	case 'a':
	    if ((optarg = getOptarg(c, &arg, &argi, argc, argv))) {
		opts->argc = parseArgs(optarg, opts->argc, &(opts->argv));
	    }
	    else return -1;
	    break;
	case 'o':
	    if (!(optarg = getOptarg(c, &arg, &argi, argc, argv)) && !(opts->outFile = openOut(optarg)))
		return -1;
	    break;
	case 'q':
	    setTraceLevel (ERROR_ERROR);  /* Don't emit warning messages */
	    break;
	case 'V':
	    sfprintf(sfstderr, "%s version %s (%s)\n",
		    Info[0], Info[1], Info[2]);
	    return 0;
	    break;
	case '?':
	    error(ERROR_USAGE|ERROR_WARNING, "%s", usage);
	    return 0;
	    break;
	default :
	    error(ERROR_WARNING, "option -%c unrecognized", c);
	    break;
	}
    }
    return argi;
}

static void
freeOpts (options* opts)
{
    int i;
    if (!opts) return;
    if (opts->outFile != sfstdout)
	sfclose (opts->outFile);
    free (opts->inFiles);
    if (opts->useFile)
	free (opts->program);
    if (opts->argc) {
	for (i = 0; i < opts->argc; i++)
	    free (opts->argv[i]);
	free (opts->argv);
    }
    free (opts);
}

/* scanArgs:
 * Parse command line options.
 */
static options* scanArgs(int argc, char **argv, gvpropts* uopts)
{
    int i, nfiles;
    char** input_filenames;
    char* arg;
    options* opts = newof(0,options,1,0);

    opts->cmdName = argv[0];
    opts->state = 1;
    setErrorId (opts->cmdName);

    /* estimate number of file names */
    nfiles = 0;
    for (i = 1; i < argc; i++)
	if (argv[i] && argv[i][0] != '-')
	    nfiles++;
    input_filenames = newof(0,char*,nfiles + 1,0);

    /* loop over arguments */
    nfiles = 0;
    for (i = 1; i < argc; ) {
	arg = argv[i++];
	if (*arg == '-') {
	    i = doFlags (arg+1, i, argc, argv, opts);
	    if (i <= 0) {
		opts->state = i;
		goto opts_done;
	    }
	} else if (arg)
	    input_filenames[nfiles++] = arg;
    }

    /* Handle additional semantics */
    if (opts->useFile == 0) {
	if (nfiles == 0) {
	    error(ERROR_ERROR,
		  "No program supplied via argument or -f option");
	    opts->state = -1;
	} else {
	    opts->program = input_filenames[0];
	    for (i = 1; i <= nfiles; i++)
		input_filenames[i-1] = input_filenames[i];
	    nfiles--;
	}
    }
    if (nfiles == 0) {
	opts->inFiles = 0;
	free (input_filenames);
	input_filenames = 0;
    }
    else
	opts->inFiles = input_filenames;

    if (!(opts->outFile))
	opts->outFile = sfstdout;

  opts_done:
    if (opts->state <= 0) {
	if (opts->state < 0)
	    error(ERROR_USAGE|ERROR_ERROR, "%s", usage);
	free (input_filenames);
    }

    return opts;
}

static void evalEdge(Gpr_t * state, comp_prog * xprog, Agedge_t * e)
{
    int i;
    case_stmt *cs;
    int okay;

    state->curobj = (Agobj_t *) e;
    for (i = 0; i < xprog->n_estmts; i++) {
	cs = xprog->edge_stmts + i;
	if (cs->guard)
	    okay = (exeval(xprog->prog, cs->guard, state)).integer;
	else
	    okay = 1;
	if (okay) {
	    if (cs->action)
		exeval(xprog->prog, cs->action, state);
	    else
		agsubedge(state->target, e, TRUE);
	}
    }
}

static void evalNode(Gpr_t * state, comp_prog * xprog, Agnode_t * n)
{
    int i;
    case_stmt *cs;
    int okay;

    state->curobj = (Agobj_t *) n;
    for (i = 0; i < xprog->n_nstmts; i++) {
	cs = xprog->node_stmts + i;
	if (cs->guard)
	    okay = (exeval(xprog->prog, cs->guard, state)).integer;
	else
	    okay = 1;
	if (okay) {
	    if (cs->action)
		exeval(xprog->prog, cs->action, state);
	    else
		agsubnode(state->target, n, TRUE);
	}
    }
}

typedef struct {
    Agnode_t *oldroot;
    Agnode_t *prev;
} nodestream;

static Agnode_t *nextNode(Gpr_t * state, nodestream * nodes)
{
    Agnode_t *np;

    if (state->tvroot != nodes->oldroot) {
	np = nodes->oldroot = state->tvroot;
    } else if (nodes->prev) {
	np = nodes->prev = agnxtnode(state->curgraph, nodes->prev);
    } else {
	np = nodes->prev = agfstnode(state->curgraph);
    }
    return np;
}

#define MARKED(x)  (((x)->iu.integer)&1)
#define MARK(x)  (((x)->iu.integer) = 1)
#define ONSTACK(x)  (((x)->iu.integer)&2)
#define PUSH(x)  (((x)->iu.integer)|=2)
#define POP(x)  (((x)->iu.integer)&=(~2))

typedef Agedge_t *(*fstedgefn_t) (Agraph_t *, Agnode_t *);
typedef Agedge_t *(*nxttedgefn_t) (Agraph_t *, Agedge_t *, Agnode_t *);

#define PRE_VISIT 1
#define POST_VISIT 2

typedef struct {
    fstedgefn_t fstedge;
    nxttedgefn_t nxtedge;
    unsigned char undirected;
    unsigned char visit;
} trav_fns;

static trav_fns DFSfns = { agfstedge, agnxtedge, 1, 0 };
static trav_fns FWDfns = { agfstout, (nxttedgefn_t) agnxtout, 0, 0 };
static trav_fns REVfns = { agfstin, (nxttedgefn_t) agnxtin, 0, 0 };

static void travBFS(Gpr_t * state, comp_prog * xprog)
{
    nodestream nodes;
    queue *q;
    ndata *nd;
    Agnode_t *n;
    Agedge_t *cure;
    Agraph_t *g = state->curgraph;

    q = mkQueue();
    nodes.oldroot = 0;
    nodes.prev = 0;
    while ((n = nextNode(state, &nodes))) {
	nd = nData(n);
	if (MARKED(nd))
	    continue;
	PUSH(nd);
	push(q, n);
	while ((n = pull(q))) {
	    nd = nData(n);
	    MARK(nd);
	    POP(nd);
	    evalNode(state, xprog, n);
	    for (cure = agfstedge(g, n); cure;
		 cure = agnxtedge(g, cure, n)) {
		nd = nData(cure->node);
		if (MARKED(nd))
		    continue;
		evalEdge(state, xprog, cure);
		if (!ONSTACK(nd)) {
		    push(q, cure->node);
		    PUSH(nd);
		}
	    }
	}
    }
    freeQ(q);
}

static void travDFS(Gpr_t * state, comp_prog * xprog, trav_fns * fns)
{
    Agnode_t *n;
    queue *stk;
    Agnode_t *curn;
    Agedge_t *cure;
    Agedge_t *entry;
    int more;
    ndata *nd;
    nodestream nodes;
    Agedgepair_t seed;

    stk = mkStack();
    nodes.oldroot = 0;
    nodes.prev = 0;
    while ((n = nextNode(state, &nodes))) {
	nd = nData(n);
	if (MARKED(nd))
	    continue;
	seed.out.node = n;
	seed.in.node = 0;
	curn = n;
	entry = &(seed.out);
	cure = 0;
	MARK(nd);
	PUSH(nd);
	if (fns->visit & PRE_VISIT)
	    evalNode(state, xprog, n);
	more = 1;
	while (more) {
	    if (cure)
		cure = fns->nxtedge(state->curgraph, cure, curn);
	    else
		cure = fns->fstedge(state->curgraph, curn);
	    if (cure) {
		if (entry == agopp(cure))	/* skip edge used to get here */
		    continue;
		nd = nData(cure->node);
		if (MARKED(nd)) {
		    /* For undirected DFS, visit an edge only if its head
		     * is on the stack, to avoid visiting it twice.
		     * This is no problem in directed DFS.
		     */
		    if (fns->undirected) {
			if (ONSTACK(nd))
			    evalEdge(state, xprog, cure);
		    } else
			evalEdge(state, xprog, cure);
		} else {
		    evalEdge(state, xprog, cure);
		    push(stk, entry);
		    entry = cure;
		    curn = cure->node;
		    cure = 0;
		    if (fns->visit & PRE_VISIT)
			evalNode(state, xprog, curn);
		    MARK(nd);
		    PUSH(nd);
		}
	    } else {
		if (fns->visit & POST_VISIT)
		    evalNode(state, xprog, curn);
		nd = nData(curn);
		POP(nd);
		cure = entry;
		entry = (Agedge_t *) pull(stk);
		if (entry)
		    curn = entry->node;
		else
		    more = 0;
	    }
	}
    }
    freeQ(stk);
}

static void travNodes(Gpr_t * state, comp_prog * xprog)
{
    Agnode_t *n;
    Agraph_t *g = state->curgraph;
    for (n = agfstnode(g); n; n = agnxtnode(g, n)) {
	evalNode(state, xprog, n);
    }
}

static void travEdges(Gpr_t * state, comp_prog * xprog)
{
    Agnode_t *n;
    Agedge_t *e;
    Agraph_t *g = state->curgraph;
    for (n = agfstnode(g); n; n = agnxtnode(g, n)) {
	for (e = agfstout(g, n); e; e = agnxtout(g, e)) {
	    evalEdge(state, xprog, e);
	}
    }
}

static void travFlat(Gpr_t * state, comp_prog * xprog)
{
    Agnode_t *n;
    Agedge_t *e;
    Agraph_t *g = state->curgraph;
    for (n = agfstnode(g); n; n = agnxtnode(g, n)) {
	evalNode(state, xprog, n);
	if (xprog->n_estmts > 0) {
	    for (e = agfstout(g, n); e; e = agnxtout(g, e)) {
		evalEdge(state, xprog, e);
	    }
	}
    }
}

/* traverse:
 */
static void traverse(Gpr_t * state, comp_prog * xprog)
{
    char *target;

    if (state->name_used) {
	sfprintf(state->tmp, "%s%d", state->tgtname, state->name_used);
	target = sfstruse(state->tmp);
    } else
	target = state->tgtname;
    state->name_used++;
    state->target = openSubg(state->curgraph, target);
    if (!state->outgraph)
	state->outgraph = state->target;

    switch (state->tvt) {
    case TV_flat:
	travFlat(state, xprog);
	break;
    case TV_bfs:
	travBFS(state, xprog);
	break;
    case TV_dfs:
	DFSfns.visit = PRE_VISIT;
	travDFS(state, xprog, &DFSfns);
	break;
    case TV_fwd:
	FWDfns.visit = PRE_VISIT;
	travDFS(state, xprog, &FWDfns);
	break;
    case TV_rev:
	REVfns.visit = PRE_VISIT;
	travDFS(state, xprog, &REVfns);
	break;
    case TV_postdfs:
	DFSfns.visit = POST_VISIT;
	travDFS(state, xprog, &DFSfns);
	break;
    case TV_postfwd:
	FWDfns.visit = POST_VISIT;
	travDFS(state, xprog, &FWDfns);
	break;
    case TV_postrev:
	REVfns.visit = POST_VISIT | PRE_VISIT;
	travDFS(state, xprog, &REVfns);
	break;
    case TV_prepostdfs:
	DFSfns.visit = POST_VISIT | PRE_VISIT;
	travDFS(state, xprog, &DFSfns);
	break;
    case TV_prepostfwd:
	FWDfns.visit = POST_VISIT | PRE_VISIT;
	travDFS(state, xprog, &FWDfns);
	break;
    case TV_prepostrev:
	REVfns.visit = POST_VISIT;
	travDFS(state, xprog, &REVfns);
	break;
    case TV_ne:
	travNodes(state, xprog);
	travEdges(state, xprog);
	break;
    case TV_en:
	travEdges(state, xprog);
	travNodes(state, xprog);
	break;
    }
}

/* addOutputGraph:
 * Append output graph to option struct.
 * We know uopts and state->outgraph are non-NULL.
 */
static void
addOutputGraph (Gpr_t* state, gvpropts* uopts)
{
    Agraph_t* g = state->outgraph;

    if ((agroot(g) == state->curgraph) && !uopts->ingraphs)
	g = (Agraph_t*)clone (0, (Agobj_t *)g);

    uopts->n_outgraphs++;
    uopts->outgraphs = oldof(uopts->outgraphs,Agraph_t*,uopts->n_outgraphs,0);
    uopts->outgraphs[uopts->n_outgraphs-1] = g;
}

static void chkClose(Agraph_t * g)
{
    gdata *data;

    data = gData(g);
    if (data->lock & 1)
	data->lock |= 2;
    else
	agclose(g);
}

static void *ing_open(char *f)
{
    return sfopen(0, f, "r");
}

static Agraph_t *ing_read(void *fp)
{
    return readG((Sfio_t *) fp);
}

static int ing_close(void *fp)
{
    return sfclose((Sfio_t *) fp);
}

static ingdisc ingDisc = { ing_open, ing_read, ing_close, 0 };

static void
setDisc (Sfio_t* sp, Sfdisc_t* dp, gvprwr fn)
{
    dp->readf = 0;
    dp->writef = (Sfwrite_f)fn;
    dp->seekf = 0;
    dp->exceptf = 0;
    dp->disc = 0;
    dp = sfdisc (sp, dp);
}

static jmp_buf jbuf;

/* gvexitf:
 * Only used if GV_USE_EXIT not set during exeval.
 * This implies setjmp/longjmp set up.
 */
static void 
gvexitf (Expr_t *handle, Exdisc_t *discipline, int v)
{
    longjmp (jbuf, v);
}

static int 
gverrorf (Expr_t *handle, Exdisc_t *discipline, int level, ...)
{
    va_list ap;

    va_start(ap, level);
    errorv((discipline
	    && handle) ? *((char **) handle) : (char *) handle, level, ap);
    va_end(ap);

    if (level >= ERROR_ERROR) {
	Gpr_t *state = (Gpr_t*)(discipline->user);
	if (state->flags & GV_USE_EXIT)
            exit(1);
	else if (state->flags & GV_USE_JUMP)
	    longjmp (jbuf, 1);
    }

    return 0;
}

/* gvpr:
 * main loop for gvpr.
 * Return 0 on success; non-zero on error.
 *
 * FIX:
 *  - close non-source/non-output graphs
 */
int gvpr (int argc, char *argv[], gvpropts * uopts)
{
    Sfdisc_t errdisc;
    Sfdisc_t outdisc;
    parse_prog *prog = 0;
    ingraph_state *ing;
    comp_prog *xprog = 0;
    Gpr_t *state = 0;
    gpr_info info;
    int rv = 0;
    options* opts = 0;
    int incoreGraphs;

    setErrorErrors (0);
    ingDisc.dflt = sfstdin;
    if (uopts) {
	if (uopts->out) setDisc (sfstdout, &outdisc, uopts->out);
	if (uopts->err) setDisc (sfstderr, &errdisc, uopts->err);
    }

    opts = scanArgs(argc, argv, uopts);
    if (opts->state <= 0) {
	rv = opts->state;
	goto finish;
    }

    prog = parseProg(opts->program, opts->useFile);
    if (!prog) {
	rv = 1;
	goto finish;
    }
    info.outFile = opts->outFile;
    info.argc = opts->argc;
    info.argv = opts->argv;
    info.errf = (Exerror_f)gverrorf;
    if (uopts) 
	info.flags = uopts->flags; 
    else
	info.flags = 0;
    if ((uopts->flags & GV_USE_EXIT))
	info.exitf = 0;
    else
	info.exitf = gvexitf;
    state = openGPRState(&info);
    if (!state) {
	rv = 1;
	goto finish;
    }
    xprog = compileProg(prog, state, opts->compflags);
    if (!xprog) {
	rv = 1;
	goto finish;
    }

    initGPRState(state, xprog->prog->vm);
    
    if ((uopts->flags & GV_USE_OUTGRAPH)) {
	uopts->outgraphs = 0;
	uopts->n_outgraphs = 0;
    }

    if (!(uopts->flags & GV_USE_EXIT)) {
	state->flags |= GV_USE_JUMP;
	if ((rv = setjmp (jbuf))) {
	    goto finish;
	}
    }

    if (uopts && uopts->ingraphs)
	incoreGraphs = 1;
    else
	incoreGraphs = 0;

    /* do begin */
    if (xprog->begin_stmt)
	exeval(xprog->prog, xprog->begin_stmt, state);

    /* if program is not null */
    if (usesGraph(xprog)) {
	if (uopts && uopts->ingraphs)
	    ing = newIngGraphs(0, uopts->ingraphs, &ingDisc);
	else
	    ing = newIng(0, opts->inFiles, &ingDisc);
	
	while ((state->curgraph = nextGraph(ing))) {
	    state->infname = fileName(ing);

	    /* begin graph */
	    if (incoreGraphs && (opts->compflags & CLONE))
		state->curgraph = clone (0, (Agobj_t*)(state->curgraph));
	    state->curobj = (Agobj_t *) state->curgraph;
	    state->tvroot = 0;
	    if (xprog->begg_stmt)
		exeval(xprog->prog, xprog->begg_stmt, state);

	    /* walk graph */
	    if (walksGraph(xprog))
		traverse(state, xprog);

	    /* end graph */
	    state->curobj = (Agobj_t *) state->curgraph;
	    if (xprog->endg_stmt)
		exeval(xprog->prog, xprog->endg_stmt, state);

	    /* if $O == $G and $T is empty, delete $T */
	    if ((state->outgraph == state->curgraph) &&
		(state->target) && !agnnodes(state->target))
		agdelete(state->curgraph, state->target);

	    /* output graph, if necessary
	     * For this, the outgraph must be defined, and either
	     * be non-empty or the -c option was used.
	     */
	    if (state->outgraph && (agnnodes(state->outgraph)
				    || (opts->compflags & SRCOUT))) {
		if (uopts && (uopts->flags & GV_USE_OUTGRAPH))
		    addOutputGraph (state, uopts);
		else
		    agwrite(state->outgraph, opts->outFile);
	    }

	    if (!incoreGraphs)
		chkClose(state->curgraph);
	    state->target = 0;
	    state->outgraph = 0;
	}
    }

	/* do end */
    state->curgraph = 0;
    state->curobj = 0;
    if (xprog->end_stmt)
	exeval(xprog->prog, xprog->end_stmt, state);

  finish:
    /* free all allocated resources */
    freeParseProg (prog);
    freeCompileProg (xprog);
    closeGPRState(state);
    if (ing) closeIngraph (ing);
    freeOpts (opts);

    if (uopts) {
	if (uopts->out) sfdisc (sfstdout, 0);
	if (uopts->err) sfdisc (sfstderr, 0);
    }

    return rv;
}

