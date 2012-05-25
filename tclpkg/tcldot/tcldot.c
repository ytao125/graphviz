/* $Id$ $Revision$ */
/* vim:set shiftwidth=4 ts=8: */

/*************************************************************************
 * Copyright (c) 2011 AT&T Intellectual Property 
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors: See CVS logs. Details at http://www.graphviz.org/
 *************************************************************************/


/* avoid compiler warnings with template changes in Tcl8.4 */
/*    specifically just the change to Tcl_CmdProc */
#define USE_NON_CONST
#include <tcl.h>
#include "render.h"
#include "gvc.h"
#include "gvio.h"
#include "tclhandle.h"

#ifndef CONST84
#define CONST84
#endif

/* ******* not ready yet
#if (TCL_MAJOR_VERSION > 7)
#define TCLOBJ
#if (TCL_MAJOR_VERSION == 8) && (TCL_MINOR_VERSION == 0)
char *
Tcl_GetString(Tcl_Obj *obj) {
    int len;
	return (Tcl_GetStringFromObj(obj, &len));
}
#else
#define UTF8
#endif
#endif
********* */

typedef struct {
#ifdef WITH_CGRAPH
    Agdisc_t mydisc;    // must be first to allow casting mydisc to mycontext
#endif
    void *graphTblPtr, *nodeTblPtr, *edgeTblPtr;
    Tcl_Interp *interp;
    GVC_t *gvc;
} mycontext_t;

/*  Globals */

#if HAVE_LIBGD
extern void *GDHandleTable;
extern int Gdtclft_Init(Tcl_Interp *);
#endif

#ifndef WITH_CGRAPH
#undef AGID
#define AGID(x) ((x)->handle)
#endif

#ifdef WITH_CGRAPH

// forward declaractions
static int graphcmd(ClientData clientData, Tcl_Interp * interp,
#ifndef TCLOBJ
		    int argc, char *argv[]
#else
		    int argc, Tcl_Obj * CONST objv[]
#endif
    );
static int nodecmd(ClientData clientData, Tcl_Interp * interp,
#ifndef TCLOBJ
		    int argc, char *argv[]
#else
		    int argc, Tcl_Obj * CONST objv[]
#endif
    );
static int edgecmd(ClientData clientData, Tcl_Interp * interp,
#ifndef TCLOBJ
		    int argc, char *argv[]
#else
		    int argc, Tcl_Obj * CONST objv[]
#endif
    );

// Agiddisc functions
static void *myiddisc_open(Agraph_t *g, Agdisc_t *disc) {
	fprintf(stderr,"myiddisc_open:\n");
        return (void *)disc;
}
static long myiddisc_map(void *state, int objtype, char *str, unsigned long *id, int createflag) {
    	mycontext_t *mycontext = (mycontext_t *)state;
	Tcl_Interp *interp = mycontext->interp;
	Tcl_CmdProc *proc = NULL;
	void *tclhandleTblPtr = NULL;
	int rc = 1; // init to success

        switch (objtype) {
                case AGRAPH: tclhandleTblPtr = mycontext->graphTblPtr; proc = graphcmd; break;
                case AGNODE: tclhandleTblPtr = mycontext->nodeTblPtr; proc = nodecmd; break;
                case AGINEDGE:
                case AGOUTEDGE: tclhandleTblPtr = mycontext->edgeTblPtr; proc=edgecmd; break;
        }
	if (createflag) {
		tclhandleAlloc(tclhandleTblPtr, Tcl_GetStringResult(interp), id);
#ifndef TCLOBJ
		Tcl_CreateCommand(interp, Tcl_GetStringResult(interp), proc, (ClientData) mycontext, (Tcl_CmdDeleteProc *) NULL);
#else
		Tcl_CreateObjCommand(interp, Tcl_GetStringResult(interp), proc, (ClientData) mycontext, (Tcl_CmdDeleteProc *) NULL);
#endif           
	}
	else {
		rc = 0;    // FIXME  - not sure about this
	}
        fprintf(stderr,"myiddisc_map: objtype %d, str \"%s\", id %lu, createflag %d, rc = %d\n", objtype, str, *id, createflag, rc);
        return rc;
}
static long myiddisc_alloc(void *state, int objtype, unsigned long id) {
//    	mycontext_t *mycontext = (mycontext_t *)state;

        switch (objtype) {
                case AGRAPH: break;
                case AGNODE: break;
                case AGINEDGE:
                case AGOUTEDGE: break;
        }
        fprintf(stderr,"myiddisc_alloc: objtype %d, id %lu\n", objtype, id);
        return 0;
}
static void myiddisc_free(void *state, int objtype, unsigned long id) {
    	mycontext_t *mycontext = (mycontext_t *)state;
	Tcl_Interp *interp = mycontext->interp;
	char buf[32];
	void *tclhandleTblePtr = NULL;

        switch (objtype) {
                case AGRAPH: tclhandleTblePtr = mycontext->graphTblPtr; break;
                case AGNODE: tclhandleTblePtr = mycontext->nodeTblPtr; break;
                case AGINEDGE:
                case AGOUTEDGE: tclhandleTblePtr = mycontext->edgeTblPtr; break;
        }
	tclhandleString(tclhandleTblePtr, buf, id);
	Tcl_DeleteCommand(interp, buf);
	tclhandleFreeIndex(tclhandleTblePtr, id);
        fprintf(stderr,"myiddisc_free: objtype %d, id %lu\n", objtype, id);
}
static char *myiddisc_print(void *state, int objtype, unsigned long id) {
//    	mycontext_t *mycontext = (mycontext_t *)state;
#if 0
        static char buf[64];
        switch (objtype) {
                case AGRAPH: sprintf(buf, "graph%lu", id); break;
                case AGNODE: sprintf(buf, "node%lu", id); break;
                case AGINEDGE:
                case AGOUTEDGE: sprintf(buf, "edge%lu", id); break;
        }
        fprintf(stderr,"myiddisc_print: objtype %d, id %lu\n", objtype, id);
        return buf;
#else
	return NIL(char*);
#endif
}
static void myiddisc_close(void *state) {
//    	mycontext_t *mycontext = (mycontext_t *)state;
        fprintf(stderr,"myiddisc_close:\n");
}
static Agiddisc_t myiddisc = {
        myiddisc_open,
        myiddisc_map,
        myiddisc_alloc,
        myiddisc_free,
        myiddisc_print,
        myiddisc_close
};

#endif // WITH_CGRAPH

static size_t Tcldot_string_writer(GVJ_t *job, const char *s, size_t len)
{
    Tcl_AppendResult((Tcl_Interp*)(job->context), s, NULL);
    return len;
}

static size_t Tcldot_channel_writer(GVJ_t *job, const char *s, size_t len)
{
    return Tcl_Write((Tcl_Channel)(job->output_file), s, len);
}

static void reset_layout(GVC_t *gvc, Agraph_t * sg)
{
    Agraph_t *g = agroot(sg);

    if (GD_drawing(g)) {	/* only cleanup once between layouts */
	gvFreeLayout(gvc, g);
	GD_drawing(g) = NULL;
    }
}

#ifdef WITH_CGRAPH
static void deleteEdges(Agraph_t * g, Agnode_t * n)
{
    Agedge_t *e, *e1;

    e = agfstedge(g, n);
    while (e) {
	e1 = agnxtedge(g, e, n);
	agdelete(agroot(g), e);
	e = e1;
    }
}

static void deleteNodes(Agraph_t * g)
{
    Agnode_t *n, *n1;

    n = agfstnode(g);
    while (n) {
	deleteEdges(agroot(g), n);
	n1 = agnxtnode(g, n);
	agdelete(agroot(g), n);
	n = n1;
    }
}
static void deleteGraph(Agraph_t * g)
{
    Agraph_t *sg;

    for (sg = agfstsubg (g); sg; sg = agnxtsubg (sg)) {
	deleteGraph(sg);
    }
    if (g == agroot(g)) {
	agclose(g);
    } else {
	agdelsubg(agroot(g), g);
    }
}
#else
static void deleteEdges(mycontext_t * mycontext, Agraph_t * g, Agnode_t * n)
{
    Agedge_t **ep, *e, *e1;
    char buf[16];

    e = agfstedge(g, n);
    while (e) {
	tclhandleString(mycontext->edgeTblPtr, buf, AGID(e));
	Tcl_DeleteCommand(mycontext->interp, buf);
	ep = (Agedge_t **) tclhandleXlateIndex(mycontext->edgeTblPtr, AGID(e));
	if (!ep)
	    fprintf(stderr, "Bad entry in edgeTbl\n");
	tclhandleFreeIndex(mycontext->edgeTblPtr, AGID(e));
	e1 = agnxtedge(g, e, n);
	agdelete(agroot(g), e);
	e = e1;
    }
}
static void deleteNodes(mycontext_t * mycontext, Agraph_t * g)
{
    Agnode_t **np, *n, *n1;
    char buf[16];

    n = agfstnode(g);
    while (n) {
	tclhandleString(mycontext->nodeTblPtr, buf, AGID(n));
	Tcl_DeleteCommand(mycontext->interp, buf);
	np = (Agnode_t **) tclhandleXlateIndex(mycontext->nodeTblPtr, AGID(n));
	if (!np)
	    fprintf(stderr, "Bad entry in nodeTbl\n");
	tclhandleFreeIndex(mycontext->nodeTblPtr, AGID(n));
	deleteEdges(mycontext, agroot(g), n);
	n1 = agnxtnode(g, n);
	agdelete(agroot(g), n);
	n = n1;
    }
}
static void deleteGraph(mycontext_t * mycontext, Agraph_t * g)
{
    Agraph_t **sgp;
    Agedge_t *e;
    char buf[16];

    if (g->meta_node) {
	for (e = agfstout(g->meta_node->graph, g->meta_node); e;
	     e = agnxtout(g->meta_node->graph, e)) {
	    deleteGraph(mycontext, agusergraph(aghead(e)));
	}
	tclhandleString(mycontext->graphTblPtr, buf, AGID(g));
	Tcl_DeleteCommand(mycontext->interp, buf);
	sgp = (Agraph_t **) tclhandleXlateIndex(mycontext->graphTblPtr, AGID(g));
	if (!sgp)
	    fprintf(stderr, "Bad entry in graphTbl\n");
	tclhandleFreeIndex(mycontext->graphTblPtr, AGID(g));
	if (g == agroot(g)) {
	    agclose(g);
	} else {
	    agdelete(g->meta_node->graph, g->meta_node);
	}
    } else {
	fprintf(stderr, "Subgraph has no meta_node\n");
    }
}
#endif

static void setgraphattributes(Agraph_t * g, char *argv[], int argc)
{
    int i;
    Agsym_t *a;

    for (i = 0; i < argc; i++) {
	if (!(a = agfindgraphattr(agroot(g), argv[i])))
#ifndef WITH_CGRAPH
	    a = agraphattr(agroot(g), argv[i], "");
	agxset(g, a->index, argv[++i]);
#else
	    a = agattr(agroot(g), AGRAPH, argv[i], "");
	agxset(g, a, argv[++i]);
#endif
    }
}

static void
setedgeattributes(Agraph_t * g, Agedge_t * e, char *argv[], int argc)
{
    int i;
    Agsym_t *a;

    for (i = 0; i < argc; i++) {
	/* silently ignore attempts to modify "key" */
	if (strcmp(argv[i], "key") == 0) {
	    i++;
	    continue;
	}
	if (!(a = agfindedgeattr(g, argv[i])))
#ifndef WITH_CGRAPH
	    a = agedgeattr(agroot(g), argv[i], "");
	agxset(e, a->index, argv[++i]);
#else
	    a = agattr(agroot(g), AGEDGE, argv[i], "");
	agxset(e, a, argv[++i]);
#endif
    }
}

static void
setnodeattributes(Agraph_t * g, Agnode_t * n, char *argv[], int argc)
{
    int i;
    Agsym_t *a;

    for (i = 0; i < argc; i++) {
	if (!(a = agfindnodeattr(g, argv[i])))
#ifndef WITH_CGRAPH
	    a = agnodeattr(agroot(g), argv[i], "");
	agxset(n, a->index, argv[++i]);
#else
	    a = agattr(agroot(g), AGNODE, argv[i], "");
	agxset(n, a, argv[++i]);
#endif
    }
}

#ifdef WITH_CGRAPH
static void listGraphAttrs (Tcl_Interp * interp, Agraph_t* g)
{
    Agsym_t *a = NULL;
    while ((a = agnxtattr(g, AGRAPH, a))) {
	Tcl_AppendElement(interp, a->name);
    }
}
static void listNodeAttrs (Tcl_Interp * interp, Agraph_t* g)
{
    Agsym_t *a = NULL;
    while ((a = agnxtattr(g, AGNODE, a))) {
	Tcl_AppendElement(interp, a->name);
    }
}
static void listEdgeAttrs (Tcl_Interp * interp, Agraph_t* g)
{
    Agsym_t *a = NULL;
    while ((a = agnxtattr(g, AGEDGE, a))) {
	Tcl_AppendElement(interp, a->name);
    }
}
#else
static void listGraphAttrs (Tcl_Interp * interp, Agraph_t* g)
{
    int i;
    Agsym_t *a;

    for (i = 0; i < dtsize(g->univ->globattr->dict); i++) {
	a = g->univ->globattr->list[i];
	Tcl_AppendElement(interp, a->name);
    }
}
static void listNodeAttrs (Tcl_Interp * interp, Agraph_t* g)
{
    int i;
    Agsym_t *a;

    for (i = 0; i < dtsize(g->univ->nodeattr->dict); i++) {
	a = g->univ->nodeattr->list[i];
	Tcl_AppendElement(interp, a->name);
    }
}
static void listEdgeAttrs (Tcl_Interp * interp, Agraph_t* g)
{
    int i;
    Agsym_t *a;

    for (i = 0; i < dtsize(g->univ->edgeattr->dict); i++) {
	a = g->univ->edgeattr->list[i];
	Tcl_AppendElement(interp, a->name);
    }
}
#endif

static int edgecmd(ClientData clientData, Tcl_Interp * interp,
#ifndef TCLOBJ
		   int argc, char *argv[]
#else				/* TCLOBJ */
		   int argc, Tcl_Obj * CONST objv[]
#endif				/* TCLOBJ */
    )
{
    char c, buf[16], *s, **argv2;
    int i, j, length, argc2;
    Agraph_t *g;
    Agedge_t **ep, *e;
    Agsym_t *a;
    mycontext_t *mycontext = (mycontext_t *)clientData;
    GVC_t *gvc = mycontext->gvc;

    if (argc < 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
			 argv[0], "\" option ?arg arg ...?",
			 NULL);
	return TCL_ERROR;
    }
    if (!(ep = (Agedge_t **) tclhandleXlate(mycontext->edgeTblPtr, argv[0]))) {
	Tcl_AppendResult(interp, " \"", argv[0], "\"", NULL);
	return TCL_ERROR;
    }
    e = *ep;
    g = agraphof(agtail(e));

    c = argv[1][0];
    length = strlen(argv[1]);

    if ((c == 'd') && (strncmp(argv[1], "delete", length) == 0)) {
#ifndef WITH_CGRAPH
	tclhandleFreeIndex(mycontext->edgeTblPtr, AGID(e));
	Tcl_DeleteCommand(interp, argv[0]);
#endif
	agdelete(g, e);
	reset_layout(gvc, g);
	return TCL_OK;

    } else if ((c == 'l')
	       && (strncmp(argv[1], "listattributes", length) == 0)) {
	listEdgeAttrs (interp, g);
	return TCL_OK;

    } else if ((c == 'l') && (strncmp(argv[1], "listnodes", length) == 0)) {
	tclhandleString(mycontext->nodeTblPtr, buf, AGID(agtail(e)));
	Tcl_AppendElement(interp, buf);
	tclhandleString(mycontext->nodeTblPtr, buf, AGID(aghead(e)));
	Tcl_AppendElement(interp, buf);
	return TCL_OK;

    } else if ((c == 'q')
	       && (strncmp(argv[1], "queryattributes", length) == 0)) {
	for (i = 2; i < argc; i++) {
	    if (Tcl_SplitList
		(interp, argv[i], &argc2,
		 (CONST84 char ***) &argv2) != TCL_OK)
		return TCL_ERROR;
	    for (j = 0; j < argc2; j++) {
		if ((a = agfindedgeattr(g, argv2[j]))) {
#ifndef WITH_CGRAPH
		    Tcl_AppendElement(interp, agxget(e, a->index));
#else
		    Tcl_AppendElement(interp, agxget(e, a));
#endif
		} else {
		    Tcl_AppendResult(interp, " No attribute named \"",
				     argv2[j], "\"", NULL);
		    return TCL_ERROR;
		}
	    }
	    Tcl_Free((char *) argv2);
	}
	return TCL_OK;

    } else if ((c == 'q')
	       && (strncmp(argv[1], "queryattributevalues", length) ==
		   0)) {
	for (i = 2; i < argc; i++) {
	    if (Tcl_SplitList
		(interp, argv[i], &argc2,
		 (CONST84 char ***) &argv2) != TCL_OK)
		return TCL_ERROR;
	    for (j = 0; j < argc2; j++) {
		if ((a = agfindedgeattr(g, argv2[j]))) {
		    Tcl_AppendElement(interp, argv2[j]);
#ifndef WITH_CGRAPH
		    Tcl_AppendElement(interp, agxget(e, a->index));
#else
		    Tcl_AppendElement(interp, agxget(e, a));
#endif
		} else {
		    Tcl_AppendResult(interp, " No attribute named \"", argv2[j], "\"", NULL);
		    return TCL_ERROR;
		}
	    }
	    Tcl_Free((char *) argv2);
	}
	return TCL_OK;

    } else if ((c == 's')
	       && (strncmp(argv[1], "setattributes", length) == 0)) {
	if (argc == 3) {
	    if (Tcl_SplitList
		(interp, argv[2], &argc2,
		 (CONST84 char ***) &argv2) != TCL_OK)
		return TCL_ERROR;
	    if ((argc2 == 0) || (argc2 % 2)) {
		Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0],
				 "\" setattributes attributename attributevalue ?attributename attributevalue? ?...?",
				 NULL);
		Tcl_Free((char *) argv2);
		return TCL_ERROR;
	    }
	    setedgeattributes(agroot(g), e, argv2, argc2);
	    Tcl_Free((char *) argv2);
	} else {
	    if ((argc < 4) || (argc % 2)) {
		Tcl_AppendResult(interp, "wrong # args: should be \"",
				 argv[0],
				 "\" setattributes attributename attributevalue ?attributename attributevalue? ?...?",
				 NULL);
		return TCL_ERROR;
	    }
	    setedgeattributes(agroot(g), e, &argv[2], argc - 2);
	}
	reset_layout(gvc, g);
	return TCL_OK;

    } else if ((c == 's') && (strncmp(argv[1], "showname", length) == 0)) {
	if (agisdirected(g))
	    s = "->";
	else
	    s = "--";
	Tcl_AppendResult(interp,
			 agnameof(agtail(e)), s, agnameof(aghead(e)), NULL);
	return TCL_OK;

    } else {
	Tcl_AppendResult(interp, "bad option \"", argv[1],
			 "\": must be one of:",
			 "\n\tdelete, listattributes, listnodes,",
			 "\n\tueryattributes, queryattributevalues,",
			 "\n\tsetattributes, showname", NULL);
	return TCL_ERROR;
    }
}

static int nodecmd(ClientData clientData, Tcl_Interp * interp,
#ifndef TCLOBJ
		   int argc, char *argv[]
#else				/* TCLOBJ */
		   int argc, Tcl_Obj * CONST objv[]
#endif				/* TCLOBJ */
    )
{
    unsigned long id;
    char c, buf[16], **argv2;
    int i, j, length, argc2;
    Agraph_t *g;
    Agnode_t **np, *n, *head;
    Agedge_t **ep, *e;
    Agsym_t *a;
    mycontext_t *mycontext = (mycontext_t *)clientData;
    GVC_t *gvc = mycontext->gvc;

    if (argc < 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
			 argv[0], " option ?arg arg ...?\"",
			 NULL);
	return TCL_ERROR;
    }
    if (!(np = (Agnode_t **) tclhandleXlate(mycontext->nodeTblPtr, argv[0]))) {
	Tcl_AppendResult(interp, " \"", argv[0], "\"", NULL);
	return TCL_ERROR;
    }
    n = *np;
    g = agraphof(n);

    c = argv[1][0];
    length = strlen(argv[1]);


    if ((c == 'a') && (strncmp(argv[1], "addedge", length) == 0)) {
	if ((argc < 3) || (!(argc % 2))) {
	    Tcl_AppendResult(interp, "wrong # args: should be \"",
			     argv[0],
			     " addedge head ?attributename attributevalue? ?...?\"",
			     NULL);
	    return TCL_ERROR;
	}
	if (!(np = (Agnode_t **) tclhandleXlate(mycontext->nodeTblPtr, argv[2]))) {
	    if (!(head = agfindnode(g, argv[2]))) {
		Tcl_AppendResult(interp, "Head node \"", argv[2],
				 "\" not found.", NULL);
		return TCL_ERROR;
	    }
	} else {
	    head = *np;
	    if (agroot(g) != agroot(agraphof(head))) {
		Tcl_AppendResult(interp, "Nodes ", argv[0], " and ",
				 argv[2], " are not in the same graph.",
				 NULL);
		return TCL_ERROR;
	    }
	}
#ifdef WITH_CGRAPH
	e = agedge(g, n, head, NULL, 1);
#else
	e = agedge(g, n, head);
#endif
	if (!
	    (ep = (Agedge_t **) tclhandleXlateIndex(mycontext->edgeTblPtr, AGID(e)))
	    || *ep != e) {
	    ep = (Agedge_t **) tclhandleAlloc(mycontext->edgeTblPtr, Tcl_GetStringResult(interp),
					      &id);
	    *ep = e;
	    AGID(e) = id;
#ifndef TCLOBJ
	    Tcl_CreateCommand(interp, Tcl_GetStringResult(interp), edgecmd,
			      (ClientData) mycontext,
			      (Tcl_CmdDeleteProc *) NULL);
#else				/* TCLOBJ */
	    Tcl_CreateObjCommand(interp, Tcl_GetStringResult(interp), edgecmd,
				 (ClientData) mycontext,
				 (Tcl_CmdDeleteProc *) NULL);
#endif				/* TCLOBJ */
	} else {
	    tclhandleString(mycontext->edgeTblPtr, Tcl_GetStringResult(interp), AGID(e));
	}
	setedgeattributes(agroot(g), e, &argv[3], argc - 3);
	reset_layout(gvc, g);
	return TCL_OK;

    } else if ((c == 'd') && (strncmp(argv[1], "delete", length) == 0)) {
#ifndef WITH_CGRAPH
	deleteEdges(mycontext, g, n);
	tclhandleFreeIndex(mycontext->nodeTblPtr, AGID(n));
	Tcl_DeleteCommand(interp, argv[0]);
#else
	deleteEdges(g, n);
#endif
	agdelete(g, n);
	reset_layout(gvc, g);
	return TCL_OK;

    } else if ((c == 'f') && (strncmp(argv[1], "findedge", length) == 0)) {
	if (argc < 3) {
	    Tcl_AppendResult(interp, "wrong # args: should be \"",
			     argv[0], " findedge headnodename\"",
			     NULL);
	    return TCL_ERROR;
	}
	if (!(head = agfindnode(g, argv[2]))) {
	    Tcl_AppendResult(interp, "Head node \"", argv[2],
			     "\" not found.", NULL);
	    return TCL_ERROR;
	}
	if (!(e = agfindedge(g, n, head))) {
	    tclhandleString(mycontext->nodeTblPtr, buf, AGID(head));
	    Tcl_AppendResult(interp, "Edge \"", argv[0],
			     " - ", buf, "\" not found.", NULL);
	    return TCL_ERROR;
	}
	tclhandleString(mycontext->edgeTblPtr, buf, AGID(e));
	Tcl_AppendElement(interp, buf);
	return TCL_OK;

    } else if ((c == 'l')
	       && (strncmp(argv[1], "listattributes", length) == 0)) {
	listNodeAttrs (interp, g);
	return TCL_OK;

    } else if ((c == 'l') && (strncmp(argv[1], "listedges", length) == 0)) {
	for (e = agfstedge(g, n); e; e = agnxtedge(g, e, n)) {
	    tclhandleString(mycontext->edgeTblPtr, buf, AGID(e));
	    Tcl_AppendElement(interp, buf);
	}
	return TCL_OK;

    } else if ((c == 'l')
	       && (strncmp(argv[1], "listinedges", length) == 0)) {
	for (e = agfstin(g, n); e; e = agnxtin(g, e)) {
	    tclhandleString(mycontext->edgeTblPtr, buf, AGID(e));
	    Tcl_AppendElement(interp, buf);
	}
	return TCL_OK;

    } else if ((c == 'l')
	       && (strncmp(argv[1], "listoutedges", length) == 0)) {
	for (e = agfstout(g, n); e; e = agnxtout(g, e)) {
	    tclhandleString(mycontext->edgeTblPtr, buf, AGID(e));
	    Tcl_AppendElement(interp, buf);
	}
	return TCL_OK;

    } else if ((c == 'q')
	       && (strncmp(argv[1], "queryattributes", length) == 0)) {
	for (i = 2; i < argc; i++) {
	    if (Tcl_SplitList
		(interp, argv[i], &argc2,
		 (CONST84 char ***) &argv2) != TCL_OK)
		return TCL_ERROR;
	    for (j = 0; j < argc2; j++) {
		if ((a = agfindnodeattr(g, argv2[j]))) {
#ifndef WITH_CGRAPH
		    Tcl_AppendElement(interp, agxget(n, a->index));
#else
		    Tcl_AppendElement(interp, agxget(n, a));
#endif
		} else {
		    Tcl_AppendResult(interp, " No attribute named \"",
				     argv2[j], "\"", NULL);
		    return TCL_ERROR;
		}
	    }
	    Tcl_Free((char *) argv2);
	}
	return TCL_OK;

    } else if ((c == 'q')
	       && (strncmp(argv[1], "queryattributevalues", length) ==
		   0)) {
	for (i = 2; i < argc; i++) {
	    if (Tcl_SplitList
		(interp, argv[i], &argc2,
		 (CONST84 char ***) &argv2) != TCL_OK)
		return TCL_ERROR;
	    for (j = 0; j < argc2; j++) {
		if ((a = agfindnodeattr(g, argv2[j]))) {
		    Tcl_AppendElement(interp, argv2[j]);
#ifndef WITH_CGRAPH
		    Tcl_AppendElement(interp, agxget(n, a->index));
#else
		    Tcl_AppendElement(interp, agxget(n, a));
#endif
		} else {
		    Tcl_AppendResult(interp, " No attribute named \"",
				     argv2[j], "\"", NULL);
		    return TCL_ERROR;
		}
	    }
	    Tcl_Free((char *) argv2);
	}
	return TCL_OK;

    } else if ((c == 's')
	       && (strncmp(argv[1], "setattributes", length) == 0)) {
	g = agroot(g);
	if (argc == 3) {
	    if (Tcl_SplitList
		(interp, argv[2], &argc2,
		 (CONST84 char ***) &argv2) != TCL_OK)
		return TCL_ERROR;
	    if ((argc2 == 0) || (argc2 % 2)) {
		Tcl_AppendResult(interp, "wrong # args: should be \"",
				 argv[0],
				 "\" setattributes attributename attributevalue ?attributename attributevalue? ?...?",
				 NULL);
		Tcl_Free((char *) argv2);
		return TCL_ERROR;
	    }
	    setnodeattributes(g, n, argv2, argc2);
	    Tcl_Free((char *) argv2);
	} else {
	    if ((argc < 4) || (argc % 2)) {
		Tcl_AppendResult(interp, "wrong # args: should be \"",
				 argv[0],
				 "\" setattributes attributename attributevalue ?attributename attributevalue? ?...?",
				 NULL);
		return TCL_ERROR;
	    }
	    setnodeattributes(g, n, &argv[2], argc - 2);
	}
	reset_layout(gvc, g);
	return TCL_OK;

    } else if ((c == 's') && (strncmp(argv[1], "showname", length) == 0)) {
	Tcl_SetResult(interp, agnameof(n), TCL_STATIC);
	return TCL_OK;

    } else {
	Tcl_AppendResult(interp, "bad option \"", argv[1],
			 "\": must be one of:",
			 "\n\taddedge, listattributes, listedges, listinedges,",
			 "\n\tlistoutedges, queryattributes, queryattributevalues,",
			 "\n\tsetattributes, showname.", NULL);
	return TCL_ERROR;
    }
}

static void tcldot_layout(GVC_t *gvc, Agraph_t * g, char *engine)
{
    char buf[256];
    Agsym_t *a;
    int rc;

    reset_layout(gvc, g);		/* in case previously drawn */

/* support old behaviors if engine isn't specified*/
    if (!engine || *engine == '\0') {
	if (agisdirected(g))
	    rc = gvlayout_select(gvc, "dot");
	else
	    rc = gvlayout_select(gvc, "neato");
    }
    else {
	if (strcasecmp(engine, "nop") == 0) {
	    Nop = 2;
	    PSinputscale = POINTS_PER_INCH;
	    rc = gvlayout_select(gvc, "neato");
	}
	else {
	    rc = gvlayout_select(gvc, engine);
	}
	if (rc == NO_SUPPORT)
	    rc = gvlayout_select(gvc, "dot");
    }
    if (rc == NO_SUPPORT) {
        fprintf(stderr, "Layout type: \"%s\" not recognized. Use one of:%s\n",
                engine, gvplugin_list(gvc, API_layout, engine));
        return;
    }
    gvLayoutJobs(gvc, g);

/* set bb attribute for basic layout.
 * doesn't yet include margins, scaling or page sizes because
 * those depend on the renderer being used. */
    if (GD_drawing(g)->landscape)
	sprintf(buf, "%d %d %d %d",
		ROUND(GD_bb(g).LL.y), ROUND(GD_bb(g).LL.x),
		ROUND(GD_bb(g).UR.y), ROUND(GD_bb(g).UR.x));
    else
	sprintf(buf, "%d %d %d %d",
		ROUND(GD_bb(g).LL.x), ROUND(GD_bb(g).LL.y),
		ROUND(GD_bb(g).UR.x), ROUND(GD_bb(g).UR.y));
#ifndef WITH_CGRAPH
    if (!(a = agfindgraphattr(g, "bb"))) 
	a = agraphattr(g, "bb", "");
    agxset(g, a->index, buf);
#else
    if (!(a = agattr(g, AGRAPH, "bb", NULL))) 
	a = agattr(g, AGRAPH, "bb", "");
    agxset(g, a, buf);
#endif
}

static int graphcmd(ClientData clientData, Tcl_Interp * interp,
#ifndef TCLOBJ
		    int argc, char *argv[]
#else
		    int argc, Tcl_Obj * CONST objv[]
#endif
    )
{

    Agraph_t *g, **gp, *sg, **sgp;
    Agnode_t **np, *n, *tail, *head;
    Agedge_t **ep, *e;
    Agsym_t *a;
    char c, buf[256], **argv2;
    int i, j, length, argc2, rc;
    unsigned long id;
    mycontext_t *mycontext = (mycontext_t *)clientData;
    GVC_t *gvc = mycontext->gvc;
    GVJ_t *job = gvc->job;

    if (argc < 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
			 argv[0], " option ?arg arg ...?\"",
			 NULL);
	return TCL_ERROR;
    }
    if (!(gp = (Agraph_t **) tclhandleXlate(mycontext->graphTblPtr, argv[0]))) {
	Tcl_AppendResult(interp, " \"", argv[0], "\"", NULL);
	return TCL_ERROR;
    }

    g = *gp;

    c = argv[1][0];
    length = strlen(argv[1]);

    if ((c == 'a') && (strncmp(argv[1], "addedge", length) == 0)) {
	if ((argc < 4) || (argc % 2)) {
	    Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0],
			     " addedge tail head ?attributename attributevalue? ?...?\"",
			     NULL);
	    return TCL_ERROR;
	}
	if (!(np = (Agnode_t **) tclhandleXlate(mycontext->nodeTblPtr, argv[2]))) {
	    if (!(tail = agfindnode(g, argv[2]))) {
		Tcl_AppendResult(interp, "Tail node \"", argv[2],
				 "\" not found.", NULL);
		return TCL_ERROR;
	    }
	} else {
	    tail = *np;
	    if (agroot(g) != agroot(agraphof(tail))) {
		Tcl_AppendResult(interp, "Node ", argv[2],
				 " is not in the graph.", NULL);
		return TCL_ERROR;
	    }
	}
	if (!(np = (Agnode_t **) tclhandleXlate(mycontext->nodeTblPtr, argv[3]))) {
	    if (!(head = agfindnode(g, argv[3]))) {
		Tcl_AppendResult(interp, "Head node \"", argv[3],
				 "\" not found.", NULL);
		return TCL_ERROR;
	    }
	} else {
	    head = *np;
	    if (agroot(g) != agroot(agraphof(head))) {
		Tcl_AppendResult(interp, "Node ", argv[3],
				 " is not in the graph.", NULL);
		return TCL_ERROR;
	    }
	}
#ifdef WITH_CGRAPH
	e = agedge(g, tail, head, NULL, 1);
#else
	e = agedge(g, tail, head);
#endif
	if (!(ep = (Agedge_t **) tclhandleXlateIndex(mycontext->edgeTblPtr, AGID(e))) || *ep != e) {
	    ep = (Agedge_t **) tclhandleAlloc(mycontext->edgeTblPtr, Tcl_GetStringResult(interp), &id);
	    *ep = e;
	    AGID(e) = id;
#ifndef TCLOBJ
	    Tcl_CreateCommand(interp, Tcl_GetStringResult(interp), edgecmd,
			      (ClientData) mycontext, (Tcl_CmdDeleteProc *) NULL);
#else				/* TCLOBJ */
	    Tcl_CreateObjCommand(interp, Tcl_GetStringResult(interp), edgecmd,
				 (ClientData) mycontext, (Tcl_CmdDeleteProc *) NULL);
#endif				/* TCLOBJ */
	} else {
	    tclhandleString(mycontext->edgeTblPtr, Tcl_GetStringResult(interp), AGID(e));
	}
	setedgeattributes(agroot(g), e, &argv[4], argc - 4);
	reset_layout(gvc, g);
	return TCL_OK;

    } else if ((c == 'a') && (strncmp(argv[1], "addnode", length) == 0)) {
	if (argc % 2) {
	    /* if odd number of args then argv[2] is name */
#ifdef WITH_CGRAPH
	    n = agnode(g, argv[2], 1);
#else
	    n = agnode(g, argv[2]);
	    if (!(np = (Agnode_t **) tclhandleXlateIndex(mycontext->nodeTblPtr, AGID(n))) || *np != n) {
		np = (Agnode_t **) tclhandleAlloc(mycontext->nodeTblPtr, Tcl_GetStringResult(interp), &id);
		*np = n;
		AGID(n) = id;
#ifndef TCLOBJ
		Tcl_CreateCommand(interp, Tcl_GetStringResult(interp), nodecmd,
				  (ClientData) mycontext, (Tcl_CmdDeleteProc *) NULL);
#else /* TCLOBJ */
		Tcl_CreateObjCommand(interp, Tcl_GetStringResult(interp), nodecmd,
				     (ClientData) mycontext, (Tcl_CmdDeleteProc *) NULL);
#endif /* TCLOBJ */
	    } else {
		tclhandleString(mycontext->nodeTblPtr, Tcl_GetStringResult(interp), AGID(n));
	    }
#endif
	    i = 3;
	} else {
	    /* else use handle as name */
#ifdef WITH_CGRAPH
	    n = agnode(g, Tcl_GetStringResult(interp), 1);
#else
	    np = (Agnode_t **) tclhandleAlloc(mycontext->nodeTblPtr, Tcl_GetStringResult(interp), &id);
	    n = agnode(g, Tcl_GetStringResult(interp));
	    *np = n;
	    AGID(n) = id;
#ifndef TCLOBJ
	    Tcl_CreateCommand(interp, Tcl_GetStringResult(interp), nodecmd,
			      (ClientData) mycontext, (Tcl_CmdDeleteProc *) NULL);
#else				/* TCLOBJ */
	    Tcl_CreateObjCommand(interp, Tcl_GetStringResult(interp), nodecmd,
				 (ClientData) mycontext, (Tcl_CmdDeleteProc *) NULL);
#endif				/* TCLOBJ */
#endif
	    i = 2;
	}
#ifdef WITH_CGRAPH
	np = (Agnode_t **)tclhandleXlateIndex(mycontext->nodeTblPtr, AGID(n));
    	*np = n;
#endif
	setnodeattributes(agroot(g), n, &argv[i], argc - i);
	reset_layout(gvc, g);
	return TCL_OK;

    } else if ((c == 'a')
	       && (strncmp(argv[1], "addsubgraph", length) == 0)) {
	if (argc < 2) {
	    Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0],
			     "\" addsubgraph ?name? ?attributename attributevalue? ?...?",
			     NULL);
	}
	if (argc % 2) {
	    /* if odd number of args then argv[2] is name */
#ifdef WITH_CGRAPH
	    sg = agsubg(g, argv[2], 1);
#else
	    sg = agsubg(g, argv[2]);
	    if (!  (sgp = (Agraph_t **) tclhandleXlateIndex(mycontext->graphTblPtr, AGID(sg))) || *sgp != sg) {
		sgp = (Agraph_t **) tclhandleAlloc(mycontext->graphTblPtr, Tcl_GetStringResult(interp), &id);
		*sgp = sg;
		AGID(sg) = id;
#ifndef TCLOBJ
		Tcl_CreateCommand(interp, Tcl_GetStringResult(interp), graphcmd,
				  (ClientData) mycontext, (Tcl_CmdDeleteProc *) NULL);
#else
		Tcl_CreateObjCommand(interp, Tcl_GetStringResult(interp), graphcmd,
				     (ClientData) mycontext, (Tcl_CmdDeleteProc *) NULL);
#endif
	    } else {
		tclhandleString(mycontext->graphTblPtr, Tcl_GetStringResult(interp), AGID(sg));
	    }
#endif
	    i = 3;
	} else {
	    /* else use handle as name */
#ifdef WITH_CGRAPH
	    sg = agsubg(g, Tcl_GetStringResult(interp), 1);
#else
	    sgp = (Agraph_t **) tclhandleAlloc(mycontext->graphTblPtr, Tcl_GetStringResult(interp), &id);
	    sg = agsubg(g, Tcl_GetStringResult(interp));
	    *sgp = sg;
	    AGID(sg) = id;
#ifndef TCLOBJ
	    Tcl_CreateCommand(interp, Tcl_GetStringResult(interp), graphcmd,
			      (ClientData) mycontext, (Tcl_CmdDeleteProc *) NULL);
#else
	    Tcl_CreateObjCommand(interp, Tcl_GetStringResult(interp), graphcmd,
				 (ClientData) mycontext, (Tcl_CmdDeleteProc *) NULL);
#endif
#endif
	    i = 2;
	}
#ifdef WITH_CGRAPH
	sgp = (Agraph_t **)tclhandleXlateIndex(mycontext->graphTblPtr, AGID(sg));
    	*sgp = sg;
#endif
	setgraphattributes(sg, &argv[i], argc - i);
	reset_layout(gvc, g);
	return TCL_OK;

    } else if ((c == 'c') && (strncmp(argv[1], "countnodes", length) == 0)) {
	sprintf(buf, "%d", agnnodes(g));
	Tcl_AppendResult(interp, buf, NULL);
	return TCL_OK;

    } else if ((c == 'c') && (strncmp(argv[1], "countedges", length) == 0)) {
	sprintf(buf, "%d", agnedges(g));
	Tcl_AppendResult(interp, buf, NULL);
	return TCL_OK;

    } else if ((c == 'd') && (strncmp(argv[1], "delete", length) == 0)) {
	reset_layout(gvc, g);
#ifndef WITH_CGRAPH
	deleteNodes(mycontext, g);
	deleteGraph(mycontext, g);
#else
	deleteNodes(g);
	deleteGraph(g);
#endif
	return TCL_OK;

    } else if ((c == 'f') && (strncmp(argv[1], "findedge", length) == 0)) {
	if (argc < 4) {
	    Tcl_AppendResult(interp, "wrong # args: should be \"",
			     argv[0], " findedge tailnodename headnodename\"", NULL);
	    return TCL_ERROR;
	}
	if (!(tail = agfindnode(g, argv[2]))) {
	    Tcl_AppendResult(interp, "Tail node \"", argv[2], "\" not found.", NULL);
	    return TCL_ERROR;
	}
	if (!(head = agfindnode(g, argv[3]))) {
	    Tcl_AppendResult(interp, "Head node \"", argv[3], "\" not found.", NULL);
	    return TCL_ERROR;
	}
	if (!(e = agfindedge(g, tail, head))) {
	    Tcl_AppendResult(interp, "Edge \"", argv[2], " - ", argv[3], "\" not found.", NULL);
	    return TCL_ERROR;
	}
	tclhandleString(mycontext->edgeTblPtr, buf, AGID(e));
	Tcl_AppendElement(interp, buf);
	return TCL_OK;

    } else if ((c == 'f') && (strncmp(argv[1], "findnode", length) == 0)) {
	if (argc < 3) {
	    Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0], " findnode nodename\"", NULL);
	    return TCL_ERROR;
	}
	if (!(n = agfindnode(g, argv[2]))) {
	    Tcl_AppendResult(interp, "Node not found.", NULL);
	    return TCL_ERROR;
	}
	tclhandleString(mycontext->nodeTblPtr, buf, AGID(n));
	Tcl_AppendResult(interp, buf, NULL);
	return TCL_OK;

    } else if ((c == 'l')
	       && (strncmp(argv[1], "layoutedges", length) == 0)) {
	g = agroot(g);
	if (!GD_drawing(g))
	    tcldot_layout(gvc, g, (argc > 2) ? argv[2] : NULL);
	return TCL_OK;

    } else if ((c == 'l')
	       && (strncmp(argv[1], "layoutnodes", length) == 0)) {
	g = agroot(g);
	if (!GD_drawing(g))
	    tcldot_layout(gvc, g, (argc > 2) ? argv[2] : NULL);
	return TCL_OK;

    } else if ((c == 'l')
	       && (strncmp(argv[1], "listattributes", length) == 0)) {
	listGraphAttrs(interp, g);
	return TCL_OK;

    } else if ((c == 'l')
	       && (strncmp(argv[1], "listedgeattributes", length) == 0)) {
	listEdgeAttrs (interp, g);
	return TCL_OK;

    } else if ((c == 'l')
	       && (strncmp(argv[1], "listnodeattributes", length) == 0)) {
	listNodeAttrs (interp, g);
	return TCL_OK;

    } else if ((c == 'l') && (strncmp(argv[1], "listedges", length) == 0)) {
	for (n = agfstnode(g); n; n = agnxtnode(g, n)) {
	    for (e = agfstout(g, n); e; e = agnxtout(g, e)) {
		tclhandleString(mycontext->edgeTblPtr, buf, AGID(e));
		Tcl_AppendElement(interp, buf);
	    }
	}
	return TCL_OK;

    } else if ((c == 'l') && (strncmp(argv[1], "listnodes", length) == 0)) {
	for (n = agfstnode(g); n; n = agnxtnode(g, n)) {
	    tclhandleString(mycontext->nodeTblPtr, buf, AGID(n));
	    Tcl_AppendElement(interp, buf);
	}
	return TCL_OK;

    } else if ((c == 'l')
	       && (strncmp(argv[1], "listnodesrev", length) == 0)) {
	for (n = aglstnode(g); n; n = agprvnode(g, n)) {
	    tclhandleString(mycontext->nodeTblPtr, buf, AGID(n));
	    Tcl_AppendElement(interp, buf);
	}
	return TCL_OK;

    } else if ((c == 'l')
	       && (strncmp(argv[1], "listsubgraphs", length) == 0)) {
#ifdef WITH_CGRAPH
	for (sg = agfstsubg(g); sg; sg = agnxtsubg(sg)) {
	    tclhandleString(mycontext->graphTblPtr, buf, AGID(sg));
	    Tcl_AppendElement(interp, buf);
	}
#else
	if (g->meta_node) {
	    for (e = agfstout(g->meta_node->graph, g->meta_node); e;
		 e = agnxtout(g->meta_node->graph, e)) {
		sg = agusergraph(aghead(e));
		tclhandleString(mycontext->graphTblPtr, buf, AGID(sg));
		Tcl_AppendElement(interp, buf);
	    }
	}
#endif
	return TCL_OK;

    } else if ((c == 'q')
	       && (strncmp(argv[1], "queryattributes", length) == 0)) {
	for (i = 2; i < argc; i++) {
	    if (Tcl_SplitList
		(interp, argv[i], &argc2,
		 (CONST84 char ***) &argv2) != TCL_OK)
		return TCL_ERROR;
	    for (j = 0; j < argc2; j++) {
		if ((a = agfindgraphattr(g, argv2[j]))) {
#ifndef WITH_CGRAPH
		    Tcl_AppendElement(interp, agxget(g, a->index));
#else
		    Tcl_AppendElement(interp, agxget(g, a));
#endif
		} else {
		    Tcl_AppendResult(interp, " No attribute named \"", argv2[j], "\"", NULL);
		    return TCL_ERROR;
		}
	    }
	    Tcl_Free((char *) argv2);
	}
	return TCL_OK;

    } else if ((c == 'q')
	       && (strncmp(argv[1], "queryattributevalues", length) ==
		   0)) {
	for (i = 2; i < argc; i++) {
	    if (Tcl_SplitList
		(interp, argv[i], &argc2,
		 (CONST84 char ***) &argv2) != TCL_OK)
		return TCL_ERROR;
	    for (j = 0; j < argc2; j++) {
		if ((a = agfindgraphattr(g, argv2[j]))) {
		    Tcl_AppendElement(interp, argv2[j]);
#ifndef WITH_CGRAPH
		    Tcl_AppendElement(interp, agxget(g, a->index));
#else
		    Tcl_AppendElement(interp, agxget(g, a));
#endif
		} else {
		    Tcl_AppendResult(interp, " No attribute named \"", argv2[j], "\"", NULL);
		    return TCL_ERROR;
		}
	    }
	    Tcl_Free((char *) argv2);
	}
	return TCL_OK;

    } else if ((c == 'q')
	       && (strncmp(argv[1], "queryedgeattributes", length) == 0)) {
	for (i = 2; i < argc; i++) {
	    if (Tcl_SplitList
		(interp, argv[i], &argc2,
		 (CONST84 char ***) &argv2) != TCL_OK)
		return TCL_ERROR;
	    for (j = 0; j < argc2; j++) {
		if ((a = agfindedgeattr(g, argv2[j]))) {
#ifndef WITH_CGRAPH
		    Tcl_AppendElement(interp, agxget(g->proto->e, a->index));
#else
		    Tcl_AppendElement(interp, agxget(g, a));
#endif
		} else {
		    Tcl_AppendResult(interp, " No attribute named \"", argv2[j], "\"", NULL);
		    return TCL_ERROR;
		}
	    }
	    Tcl_Free((char *) argv2);
	}
	return TCL_OK;

    } else if ((c == 'q')
	       && (strncmp(argv[1], "queryedgeattributevalues", length) == 0)) {
	for (i = 2; i < argc; i++) {
	    if (Tcl_SplitList
		(interp, argv[i], &argc2,
		 (CONST84 char ***) &argv2) != TCL_OK)
		return TCL_ERROR;
	    for (j = 0; j < argc2; j++) {
		if ((a = agfindedgeattr(g, argv2[j]))) {
		    Tcl_AppendElement(interp, argv2[j]);
#ifndef WITH_CGRAPH
		    Tcl_AppendElement(interp, agxget(g->proto->e, a->index));
#else
		    Tcl_AppendElement(interp, agxget(g, a));
#endif
		} else {
		    Tcl_AppendResult(interp, " No attribute named \"",
				     argv2[j], "\"", NULL);
		    return TCL_ERROR;
		}
	    }
	    Tcl_Free((char *) argv2);
	}
	return TCL_OK;

    } else if ((c == 'q')
	       && (strncmp(argv[1], "querynodeattributes", length) == 0)) {
	for (i = 2; i < argc; i++) {
	    if (Tcl_SplitList
		(interp, argv[i], &argc2,
		 (CONST84 char ***) &argv2) != TCL_OK)
		return TCL_ERROR;
	    for (j = 0; j < argc2; j++) {
		if ((a = agfindnodeattr(g, argv2[j]))) {
#ifndef WITH_CGRAPH
		    Tcl_AppendElement(interp, agxget(g->proto->n, a->index));
#else
		    Tcl_AppendElement(interp, agxget(g, a));
#endif
		} else {
		    Tcl_AppendResult(interp, " No attribute named \"",
				     argv2[j], "\"", NULL);
		    return TCL_ERROR;
		}
	    }
	    Tcl_Free((char *) argv2);
	}
	return TCL_OK;

    } else if ((c == 'q')
	       && (strncmp(argv[1], "querynodeattributevalues", length) ==
		   0)) {
	for (i = 2; i < argc; i++) {
	    if (Tcl_SplitList
		(interp, argv[i], &argc2,
		 (CONST84 char ***) &argv2) != TCL_OK)
		return TCL_ERROR;
	    for (j = 0; j < argc2; j++) {
		if ((a = agfindnodeattr(g, argv2[j]))) {
		    Tcl_AppendElement(interp, argv2[j]);
#ifndef WITH_CGRAPH
		    Tcl_AppendElement(interp, agxget(g->proto->n, a->index));
#else
		    Tcl_AppendElement(interp, agxget(g, a));
#endif
		} else {
		    Tcl_AppendResult(interp, " No attribute named \"", argv2[j], "\"", NULL);
		    return TCL_ERROR;
		}
	    }
	    Tcl_Free((char *) argv2);
	}
	return TCL_OK;

    } else if ((c == 'r') && (strncmp(argv[1], "render", length) == 0)) {
	char *canvas;

	if (argc < 3) {
	    canvas = "$c";
	} else {
	    canvas = argv[2];
#if 0				/* not implemented */
	    if (argc < 4) {
		tkgendata.eval = FALSE;
	    } else {
		if ((Tcl_GetBoolean(interp, argv[3], &tkgendata.eval)) !=
		    TCL_OK) {
		    Tcl_AppendResult(interp, " Invalid boolean: \"",
				     argv[3], "\"", NULL);
		    return TCL_ERROR;
		}
	    }
#endif
	}
        rc = gvjobs_output_langname(gvc, "tk");
	if (rc == NO_SUPPORT) {
	    Tcl_AppendResult(interp, " Format: \"tk\" not recognized.\n", NULL);
	    return TCL_ERROR;
	}

        gvc->write_fn = Tcldot_string_writer;
	job = gvc->job;
	job->imagedata = canvas;
	job->context = (void *)interp;
	job->external_context = TRUE;
	job->output_file = stdout;

	/* make sure that layout is done */
	g = agroot(g);
	if (!GD_drawing(g) || argc > 3)
	    tcldot_layout (gvc, g, (argc > 3) ? argv[3] : NULL);

	/* render graph TK canvas commands */
	gvc->common.viewNum = 0;
	gvRenderJobs(gvc, g);
	gvrender_end_job(job);
	gvdevice_finalize(job);
	fflush(job->output_file);
	gvjobs_delete(gvc);
	return TCL_OK;

#if HAVE_LIBGD
    } else if ((c == 'r') && (strncmp(argv[1], "rendergd", length) == 0)) {
	void **hdl;

	if (argc < 3) {
	    Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0],
			     " rendergd gdhandle ?DOT|NEATO|TWOPI|FDP|CIRCO?\"", NULL);
	    return TCL_ERROR;
	}
	rc = gvjobs_output_langname(gvc, "gd:gd:gd");
	if (rc == NO_SUPPORT) {
	    Tcl_AppendResult(interp, " Format: \"gd\" not recognized.\n", NULL);
	    return TCL_ERROR;
	}
        job = gvc->job;

	if (!  (hdl = tclhandleXlate(GDHandleTable, argv[2]))) {
	    Tcl_AppendResult(interp, "GD Image not found.", NULL);
	    return TCL_ERROR;
	}
	job->context = *hdl;
	job->external_context = TRUE;

	/* make sure that layout is done */
	g = agroot(g);
	if (!GD_drawing(g) || argc > 4)
	    tcldot_layout(gvc, g, (argc > 4) ? argv[4] : NULL);
	
	gvc->common.viewNum = 0;
	gvRenderJobs(gvc, g);
	gvrender_end_job(job);
	gvdevice_finalize(job);
	fflush(job->output_file);
	gvjobs_delete(gvc);
	Tcl_AppendResult(interp, argv[2], NULL);
	return TCL_OK;
#endif

    } else if ((c == 's')
	       && (strncmp(argv[1], "setattributes", length) == 0)) {
	if (argc == 3) {
	    if (Tcl_SplitList
		(interp, argv[2], &argc2,
		 (CONST84 char ***) &argv2) != TCL_OK)
		return TCL_ERROR;
	    if ((argc2 == 0) || (argc2 % 2)) {
		Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0],
				 "\" setattributes attributename attributevalue ?attributename attributevalue? ?...?",
				 NULL);
		Tcl_Free((char *) argv2);
		return TCL_ERROR;
	    }
	    setgraphattributes(g, argv2, argc2);
	    Tcl_Free((char *) argv2);
	    reset_layout(gvc, g);
	}
	if (argc == 4 && strcmp(argv[2], "viewport") == 0) {
	    /* special case to allow viewport to be set without resetting layout */
	    setgraphattributes(g, &argv[2], argc - 2);
	} else {
	    if ((argc < 4) || (argc % 2)) {
		Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0],
				 "\" setattributes attributename attributevalue ?attributename attributevalue? ?...?",
				 NULL);
		return TCL_ERROR;
	    }
	    setgraphattributes(g, &argv[2], argc - 2);
	    reset_layout(gvc, g);
	}
	return TCL_OK;

    } else if ((c == 's')
	       && (strncmp(argv[1], "setedgeattributes", length) == 0)) {
	if (argc == 3) {
	    if (Tcl_SplitList
		(interp, argv[2], &argc2,
		 (CONST84 char ***) &argv2) != TCL_OK)
		return TCL_ERROR;
	    if ((argc2 == 0) || (argc2 % 2)) {
		Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0],
				 "\" setedgeattributes attributename attributevalue ?attributename attributevalue? ?...?",
				 NULL);
		Tcl_Free((char *) argv2);
		return TCL_ERROR;
	    }
#ifndef WITH_CGRAPH
	    setedgeattributes(g, g->proto->e, argv2, argc2);
#else
	    setedgeattributes(g, NULL, argv2, argc2);
#endif
	    Tcl_Free((char *) argv2);
	} else {
	    if ((argc < 4) || (argc % 2)) {
		Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0],
				 "\" setedgeattributes attributename attributevalue ?attributename attributevalue? ?...?",
				 NULL);
	    }
#ifndef WITH_CGRAPH
	    setedgeattributes(g, g->proto->e, &argv[2], argc - 2);
#else
	    setedgeattributes(g, NULL, &argv[2], argc - 2);
#endif
	}
	reset_layout(gvc, g);
	return TCL_OK;

    } else if ((c == 's')
	       && (strncmp(argv[1], "setnodeattributes", length) == 0)) {
	if (argc == 3) {
	    if (Tcl_SplitList
		(interp, argv[2], &argc2,
		 (CONST84 char ***) &argv2) != TCL_OK)
		return TCL_ERROR;
	    if ((argc2 == 0) || (argc2 % 2)) {
		Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0],
				 "\" setnodeattributes attributename attributevalue ?attributename attributevalue? ?...?",
				 NULL);
		Tcl_Free((char *) argv2);
		return TCL_ERROR;
	    }
#ifndef WITH_CGRAPH
	    setnodeattributes(g, g->proto->n, argv2, argc2);
#else
	    setnodeattributes(g, NULL, argv2, argc2);
#endif
	    Tcl_Free((char *) argv2);
	} else {
	    if ((argc < 4) || (argc % 2)) {
		Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0],
				 "\" setnodeattributes attributename attributevalue ?attributename attributevalue? ?...?",
				 NULL);
	    }
#ifndef WITH_CGRAPH
	    setnodeattributes(g, g->proto->n, &argv[2], argc - 2);
#else
	    setnodeattributes(g, NULL, &argv[2], argc - 2);
#endif
	}
	reset_layout(gvc, g);
	return TCL_OK;

    } else if ((c == 's') && (strncmp(argv[1], "showname", length) == 0)) {
	Tcl_SetResult(interp, agnameof(g), TCL_STATIC);
	return TCL_OK;

    } else if ((c == 'w') && (strncmp(argv[1], "write", length) == 0)) {
	g = agroot(g);
	if (argc < 3) {
	    Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0],
	      " write fileHandle ?language ?DOT|NEATO|TWOPI|FDP|CIRCO|NOP??\"",
	      NULL);
	    return TCL_ERROR;
	}

	/* process lang first to create job */
	if (argc < 4) {
	    i = gvjobs_output_langname(gvc, "dot");
	} else {
	    i = gvjobs_output_langname(gvc, argv[3]);
	}
	if (i == NO_SUPPORT) {
	    const char *s = gvplugin_list(gvc, API_render, argv[3]);
	    Tcl_AppendResult(interp, "Bad langname: \"", argv[3], "\". Use one of:", s, NULL);
	    return TCL_ERROR;
	}

	gvc->write_fn = Tcldot_channel_writer;
	job = gvc->job;

	/* populate new job struct with output language and output file data */
	job->output_lang = gvrender_select(job, job->output_langname);

//	if (Tcl_GetOpenFile (interp, argv[2], 1, 1, &outfp) != TCL_OK)
//	    return TCL_ERROR;
//	job->output_file = (FILE *)outfp;
	
	{
	    Tcl_Channel chan;
	    int mode;

	    chan = Tcl_GetChannel(interp, argv[2], &mode);

	    if (!chan) {
	        Tcl_AppendResult(interp, "Channel not open: \"", argv[2], NULL);
	        return TCL_ERROR;
	    }
	    if (!(mode & TCL_WRITABLE)) {
	        Tcl_AppendResult(interp, "Channel not writable: \"", argv[2], NULL);
	        return TCL_ERROR;
	    }
	    job->output_file = (FILE *)chan;
	}
	job->output_filename = NULL;

	/* make sure that layout is done  - unless canonical output */
	if ((!GD_drawing(g) || argc > 4) && !(job->flags & LAYOUT_NOT_REQUIRED)) {
	    tcldot_layout(gvc, g, (argc > 4) ? argv[4] : NULL);
	}

	gvc->common.viewNum = 0;
	gvRenderJobs(gvc, g);
	gvdevice_finalize(job);
//	fflush(job->output_file);
	gvjobs_delete(gvc);
	return TCL_OK;

    } else {
	Tcl_AppendResult(interp, "bad option \"", argv[1],
	 "\": must be one of:",
	 "\n\taddedge, addnode, addsubgraph, countedges, countnodes,",
	 "\n\tlayout, listattributes, listedgeattributes, listnodeattributes,",
	 "\n\tlistedges, listnodes, listsubgraphs, render, rendergd,",
	 "\n\tqueryattributes, queryedgeattributes, querynodeattributes,",
	 "\n\tqueryattributevalues, queryedgeattributevalues, querynodeattributevalues,",
	 "\n\tsetattributes, setedgeattributes, setnodeattributes,",
	 "\n\tshowname, write.", NULL);
	return TCL_ERROR;
    }
}				/* graphcmd */

static int dotnew(ClientData clientData, Tcl_Interp * interp,
#ifndef TCLOBJ
		  int argc, char *argv[]
#else				/* TCLOBJ */
		  int argc, Tcl_Obj * CONST objv[]
#endif				/* TCLOBJ */
    )
{
    mycontext_t *mycontext = (mycontext_t *)clientData;
    Agraph_t *g, **gp;
    char c;
    int i, length;
#ifndef WITH_CGRAPH
    int kind;
    unsigned long id;
#else
    Agdesc_t kind;
#endif

    if ((argc < 2)) {
	Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0],
			 " graphtype ?graphname? ?attributename attributevalue? ?...?\"",
			 NULL);
	return TCL_ERROR;
    }
    c = argv[1][0];
    length = strlen(argv[1]);
    if ((c == 'd') && (strncmp(argv[1], "digraph", length) == 0)) {
#ifndef WITH_CGRAPH
	kind = AGDIGRAPH;
#else
	kind = Agdirected;
#endif
    } else if ((c == 'd')
	       && (strncmp(argv[1], "digraphstrict", length) == 0)) {
#ifndef WITH_CGRAPH
	kind = AGDIGRAPHSTRICT;
#else
	kind = Agstrictdirected;
#endif
    } else if ((c == 'g') && (strncmp(argv[1], "graph", length) == 0)) {
#ifndef WITH_CGRAPH
	kind = AGRAPH;
#else
	kind = Agundirected;
#endif
    } else if ((c == 'g')
	       && (strncmp(argv[1], "graphstrict", length) == 0)) {
#ifndef WITH_CGRAPH
	kind = AGRAPHSTRICT;
#else
	kind = Agstrictundirected;
#endif
    } else {
	Tcl_AppendResult(interp, "bad graphtype \"", argv[1], "\": must be one of:",
			 "\n\tdigraph, digraphstrict, graph, graphstrict.", NULL);
	return TCL_ERROR;
    }
#ifndef WITH_CGRAPH
    gp = (Agraph_t **) tclhandleAlloc(mycontext->graphTblPtr, Tcl_GetStringResult(interp), &id);
#endif
    if (argc % 2) {
	/* if odd number of args then argv[2] is name */
#ifndef WITH_CGRAPH
	g = agopen(argv[2], kind);
#else
	g = agopen(argv[2], kind, (Agdisc_t*)mycontext);
#endif
	i = 3;
    } else {
	/* else use handle as name */
#ifndef WITH_CGRAPH
	g = agopen(Tcl_GetStringResult(interp), kind);
#else
	g = agopen(Tcl_GetStringResult(interp), kind, (Agdisc_t*)mycontext);
#endif
	i = 2;
    }
#ifdef WITH_CGRAPH
    agbindrec(g, "Agraphinfo_t", sizeof(Agraphinfo_t), TRUE);
#endif
    if (!g) {
	Tcl_AppendResult(interp, "\nFailure to open graph.", NULL);
	return TCL_ERROR;
    }
#ifndef WITH_CGRAPH
    *gp = g;
    AGID(g) = id;
#else
    gp = (Agraph_t **)tclhandleXlateIndex(mycontext->graphTblPtr, AGID(g));
    *gp = g;
#endif

#ifndef WITH_CGRAPH
#ifndef TCLOBJ
    Tcl_CreateCommand(interp, Tcl_GetStringResult(interp), graphcmd,
		      (ClientData) mycontext, (Tcl_CmdDeleteProc *) NULL);
#else				/* TCLOBJ */
    Tcl_CreateObjCommand(interp, Tcl_GetStringResult(interp), graphcmd,
			 (ClientData) mycontext, (Tcl_CmdDeleteProc *) NULL);
#endif				/* TCLOBJ */
#endif
    setgraphattributes(g, &argv[i], argc - i);
    /* we use GD_drawing(g) as a flag that layout has been done.
     * so we make sure that it is initialized to "not done" */
    GD_drawing(g) = NULL;

    return TCL_OK;
}

#ifdef WITH_CGRAPH
static void
init_graphs (mycontext_t *mycontext, graph_t* g)
{
    Agraph_t *sg, **sgp;
    unsigned long id;
    char buf[16];
    Tcl_Interp *interp = mycontext->interp;

    for (sg = agfstsubg (g); sg; sg = agnxtsubg (sg))
	init_graphs (mycontext, sg);

    sgp = (Agraph_t **) tclhandleAlloc(mycontext->graphTblPtr, buf, &id);
    *sgp = g;
    AGID(g) = id;
#ifndef TCLOBJ
    Tcl_CreateCommand(interp, buf, graphcmd, (ClientData) mycontext, (Tcl_CmdDeleteProc *) NULL);
#else				/* TCLOBJ */
    Tcl_CreateObjCommand(interp, buf, graphcmd, (ClientData) mycontext, (Tcl_CmdDeleteProc *) NULL);
#endif				/* TCLOBJ */
    if (agroot(g) == g)
	Tcl_SetResult(interp, buf, TCL_VOLATILE);
}
#endif

/*
 * when a graph is read in from a file or string we need to walk
 * it to create the handles and tcl commands for each 
 * graph, subgraph, node, and edge.
 */
static int tcldot_fixup(mycontext_t *mycontext, graph_t * g)
{
#ifndef WITH_CGRAPH
    Agraph_t **gp, *sg, **sgp;
#endif
    Agnode_t *n, **np;
    Agedge_t *e, **ep;
    char buf[16];
    unsigned long id;
    Tcl_Interp *interp = mycontext->interp;

#ifdef WITH_CGRAPH
    init_graphs (mycontext, g);
#else
    if (g->meta_node) {
	for (n = agfstnode(g->meta_node->graph); n;
	     n = agnxtnode(g->meta_node->graph, n)) {
	    sg = agusergraph(n);
	    sgp = (Agraph_t **) tclhandleAlloc(mycontext->graphTblPtr, buf, &id);
	    *sgp = sg;
	    AGID(sg) = id;
#ifndef TCLOBJ
	    Tcl_CreateCommand(interp, buf, graphcmd, (ClientData) mycontext,
			      (Tcl_CmdDeleteProc *) NULL);
#else				/* TCLOBJ */
	    Tcl_CreateObjCommand(interp, buf, graphcmd, (ClientData) mycontext,
				 (Tcl_CmdDeleteProc *) NULL);
#endif				/* TCLOBJ */
	    if (sg == g)
		Tcl_SetResult(interp, buf, TCL_VOLATILE);
	}
    } else {
	gp = (Agraph_t **) tclhandleAlloc(mycontext->graphTblPtr, Tcl_GetStringResult(interp), &id);
	*gp = g;
	AGID(g) = id;
#ifndef TCLOBJ
	Tcl_CreateCommand(interp, Tcl_GetStringResult(interp), graphcmd,
			  (ClientData) mycontext, (Tcl_CmdDeleteProc *) NULL);
#else				/* TCLOBJ */
	Tcl_CreateObjCommand(interp, Tcl_GetStringResult(interp), graphcmd,
			     (ClientData) mycontext, (Tcl_CmdDeleteProc *) NULL);
#endif				/* TCLOBJ */
    }
#endif /* WITH_CGRAPH */
    for (n = agfstnode(g); n; n = agnxtnode(g, n)) {
	np = (Agnode_t **) tclhandleAlloc(mycontext->nodeTblPtr, buf, &id);
	*np = n;
	AGID(n) = id;
#ifndef TCLOBJ
	Tcl_CreateCommand(interp, buf, nodecmd,
			  (ClientData) mycontext, (Tcl_CmdDeleteProc *) NULL);
#else				/* TCLOBJ */
	Tcl_CreateObjCommand(interp, buf, nodecmd,
			     (ClientData) gvc, (Tcl_CmdDeleteProc *) NULL);
#endif				/* TCLOBJ */
	for (e = agfstout(g, n); e; e = agnxtout(g, e)) {
	    ep = (Agedge_t **) tclhandleAlloc(mycontext->edgeTblPtr, buf, &id);
	    *ep = e;
	    AGID(e) = id;
#ifndef TCLOBJ
	    Tcl_CreateCommand(interp, buf, edgecmd, (ClientData) mycontext,
			      (Tcl_CmdDeleteProc *) NULL);
#else				/* TCLOBJ */
	    Tcl_CreateObjCommand(interp, buf, edgecmd, (ClientData) gvc,
				 (Tcl_CmdDeleteProc *) NULL);
#endif				/* TCLOBJ */
	}
    }
    return TCL_OK;
}

/*
 * mygets - same api as gets for libgraph, or read for libcgraph
 *
 * gets one line at a time from a Tcl_Channel and places it in a user buffer
 *    up to a maximum of n characters
 *
 * returns pointer to obtained line in user buffer, or
 * returns NULL when last line read from memory buffer
 *
 * This is probably innefficient because it introduces
 * one more stage of line buffering during reads (at least)
 * but it is needed so that we can take full advantage
 * of the Tcl_Channel mechanism.
 */
#ifdef WITH_CGRAPH
static int mygets(void* channel, char *ubuf, int n)
{
    static Tcl_DString dstr;
    static int strpos;
    int nput;

    if (!n) {			/* a call with n==0 (from aglexinit) resets */
	*ubuf = '\0';
	strpos = 0;
	return 0;
    }

    /* 
     * the user buffer might not be big enough to hold the line.
     */
    if (strpos) {
	nput = Tcl_DStringLength(&dstr) - strpos;
	if (nput > n) {
	    /* chunk between first and last */
	    memcpy(ubuf, (strpos + Tcl_DStringValue(&dstr)), n);
	    strpos += n;
	    nput = n;
	    ubuf[n] = '\0';
	} else {
	    /* last chunk */
	    memcpy(ubuf, (strpos + Tcl_DStringValue(&dstr)), nput);
	    strpos = 0;
	}
    } else {
	Tcl_DStringFree(&dstr);
	Tcl_DStringInit(&dstr);
	if (Tcl_Gets((Tcl_Channel) channel, &dstr) < 0) {
	    /* probably EOF, but could be other read errors */
	    *ubuf = '\0';
	    return 0;
	}
	/* linend char(s) were stripped off by Tcl_Gets,
	 * append a canonical linenend. */
	Tcl_DStringAppend(&dstr, "\n", 1);
	if (Tcl_DStringLength(&dstr) > n) {
	    /* first chunk */
	    nput = n;
	    memcpy(ubuf, Tcl_DStringValue(&dstr), n);
	    strpos = n;
	} else {
	    /* single chunk */
	    nput = Tcl_DStringLength(&dstr);
	    memcpy(ubuf, Tcl_DStringValue(&dstr),nput);
	}
    }
    return nput;
}
#else
static char *mygets(char *ubuf, int n, FILE * channel)
{
    static Tcl_DString dstr;
    static int strpos;

    if (!n) {			/* a call with n==0 (from aglexinit) resets */
	*ubuf = '\0';
	strpos = 0;
	return NULL;
    }

    /* 
     * the user buffer might not be big enough to hold the line.
     */
    if (strpos) {
	if (Tcl_DStringLength(&dstr) > (n + strpos)) {
	    /* chunk between first and last */
	    strncpy(ubuf, (strpos + Tcl_DStringValue(&dstr)), n - 1);
	    strpos += (n - 1);
	    ubuf[n] = '\0';
	} else {
	    /* last chunk */
	    strcpy(ubuf, (strpos + Tcl_DStringValue(&dstr)));
	    strpos = 0;
	}
    } else {
	Tcl_DStringFree(&dstr);
	Tcl_DStringInit(&dstr);
	if (Tcl_Gets((Tcl_Channel) channel, &dstr) < 0) {
	    /* probably EOF, but could be other read errors */
	    *ubuf = '\0';
	    return NULL;
	}
	/* linend char(s) were stripped off by Tcl_Gets,
	 * append a canonical linenend. */
	Tcl_DStringAppend(&dstr, "\n", 1);
	if (Tcl_DStringLength(&dstr) >= n) {
	    /* first chunk */
	    strncpy(ubuf, Tcl_DStringValue(&dstr), n - 1);
	    strpos = n - 1;
	    ubuf[n] = '\0';
	} else {
	    /* single chunk */
	    strcpy(ubuf, Tcl_DStringValue(&dstr));
	}
    }
    return ubuf;

#if 0
    if (!n) {			/* a call with n==0 (from aglexinit) resets */
	mempos = (char *) mbuf;	/* cast from FILE* required by API */
    }

    clp = to = ubuf;
    for (i = 0; i < n - 1; i++) {	/* leave room for terminator */
	if (*mempos == '\0') {
	    if (i) {		/* if mbuf doesn't end in \n, provide one */
		*to++ = '\n';
	    } else {		/* all done */
		clp = NULL;
		mempos = NULL;
	    }
	    break;		/* last line or end-of-buffer */
	}
	if (*mempos == '\n') {
	    *to++ = *mempos++;
	    break;		/* all done with this line */
	}
	*to++ = *mempos++;	/* copy character */
    }
    *to++ = '\0';		/* place terminator in ubuf */
    return clp;
#endif
}
#endif /* WITH_CGRAPH */

#ifdef WITH_CGRAPH

Agraph_t *agread_usergets (FILE * fp, int (*usergets)(void *chan, char *buf, int bufsize))
{
    Agraph_t* g;
    Agdisc_t disc;
    Agiodisc_t ioDisc;

    ioDisc.afread = usergets;
    ioDisc.putstr = AgIoDisc.putstr;
    ioDisc.flush = AgIoDisc.flush;

    disc.mem = &AgMemDisc;
    disc.id = &AgIdDisc;
    disc.io = &ioDisc;
    g = agread (fp, &disc);
    return g;
}
#endif

static int dotread(ClientData clientData, Tcl_Interp * interp,
#ifndef TCLOBJ
		   int argc, char *argv[]
#else				/* TCLOBJ */
		   int argc, Tcl_Obj * CONST objv[]
#endif				/* TCLOBJ */
    )
{
    Agraph_t *g;
    Tcl_Channel channel;
    int mode;
    mycontext_t *mycontext = (mycontext_t *)clientData;

    if (argc < 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
			 argv[0], " fileHandle\"", NULL);
	return TCL_ERROR;
    }
    channel = Tcl_GetChannel(interp, argv[1], &mode);
    if (channel == NULL || !(mode & TCL_READABLE)) {
	Tcl_AppendResult(interp, "\nChannel \"", argv[1], "\"", "is unreadable.", NULL);
	return TCL_ERROR;
    }
    /*
     * read a graph from the channel, the channel is left open
     *   ready to read the first line after the last line of
     *   a properly parsed graph. If the graph doesn't parse
     *   during reading then the channel will be left at EOF
     */
    g = agread_usergets((FILE *) channel, (mygets));
    if (!g) {
	Tcl_AppendResult(interp, "\nFailure to read graph \"", argv[1], "\"", NULL);
	if (agerrors()) {
	    Tcl_AppendResult(interp, " because of syntax errors.", NULL);
	}
	return TCL_ERROR;
    }
    if (agerrors()) {
	Tcl_AppendResult(interp, "\nSyntax errors in file \"", argv[1], " \"", NULL);
	return TCL_ERROR;
    }
    /* we use GD_drawing(g) as a flag that layout has been done.
     * so we make sure that it is initialized to "not done" */
    GD_drawing(g) = NULL;

    return (tcldot_fixup(mycontext, g));
}

static int dotstring(ClientData clientData, Tcl_Interp * interp,
#ifndef TCLOBJ
		     int argc, char *argv[]
#else				/* TCLOBJ */
		     int argc, Tcl_Obj * CONST objv[]
#endif				/* TCLOBJ */
    )
{
    Agraph_t *g;
    mycontext_t *mycontext = (mycontext_t *)clientData;

    if (argc < 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0], " string\"", NULL);
	return TCL_ERROR;
    }
    if (!(g = agmemread(argv[1]))) {
	Tcl_AppendResult(interp, "\nFailure to read string \"", argv[1], "\"", NULL);
	if (agerrors()) {
	    Tcl_AppendResult(interp, " because of syntax errors.", NULL);
	}
	return TCL_ERROR;
    }
    if (agerrors()) {
	Tcl_AppendResult(interp, "\nSyntax errors in string \"", argv[1], " \"", NULL);
	return TCL_ERROR;
    }
    /* we use GD_drawing(g) as a flag that layout has been done.
     * so we make sure that it is initialized to "not done" */
    GD_drawing(g) = NULL;

    return (tcldot_fixup(mycontext, g));
}

#if defined(_BLD_tcldot) && defined(_DLL)
__EXPORT__
#endif
int Tcldot_Init(Tcl_Interp * interp)
{
    mycontext_t *mycontext;
    GVC_t *gvc;

    mycontext = calloc(1, sizeof(mycontext_t));
    if (!mycontext)
	return TCL_ERROR;

    mycontext->interp = interp;
#ifdef WITH_CGRAPH    
    mycontext->mydisc.id = &myiddisc;
#endif

#ifdef USE_TCL_STUBS
    if (Tcl_InitStubs(interp, TCL_VERSION, 0) == NULL) {
	return TCL_ERROR;
    }
#else
    if (Tcl_PkgRequire(interp, "Tcl", TCL_VERSION, 0) == NULL) {
	return TCL_ERROR;
    }
#endif
    if (Tcl_PkgProvide(interp, "Tcldot", VERSION) != TCL_OK) {
	return TCL_ERROR;
    }

#if HAVE_LIBGD
    Gdtclft_Init(interp);
#endif

#ifdef WITH_CGRAPH
    /* set persistent attributes here */
    agattr(NULL, AGNODE, "label", NODENAME_ESC);
#else
    aginit();
    agsetiodisc(NULL, gvfwrite, gvferror);
    /* set persistent attributes here */
    agnodeattr(NULL, "label", NODENAME_ESC);
#endif

    /* create a GraphViz Context and pass a pointer to it in clientdata */
    gvc = gvNEWcontext(lt_preloaded_symbols, DEMAND_LOADING);
    mycontext->gvc = gvc;

    /* configure for available plugins */
    gvconfig(gvc, FALSE);

#ifndef TCLOBJ
    Tcl_CreateCommand(interp, "dotnew", dotnew,
		      (ClientData) mycontext, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, "dotread", dotread,
		      (ClientData) mycontext, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, "dotstring", dotstring,
		      (ClientData) mycontext, (Tcl_CmdDeleteProc *) NULL);
#else				/* TCLOBJ */
    Tcl_CreateObjCommand(interp, "dotnew", dotnew,
			 (ClientData) mycontext, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateObjCommand(interp, "dotread", dotread,
			 (ClientData) mycontext, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateObjCommand(interp, "dotstring", dotstring,
			 (ClientData) mycontext, (Tcl_CmdDeleteProc *) NULL);
#endif				/* TCLOBJ */

    mycontext->graphTblPtr = (void *) tclhandleInit("graph", sizeof(Agraph_t *), 10);
    mycontext->nodeTblPtr = (void *) tclhandleInit("node", sizeof(Agnode_t *), 100);
    mycontext->edgeTblPtr = (void *) tclhandleInit("edge", sizeof(Agedge_t *), 100);


    return TCL_OK;
}

int Tcldot_SafeInit(Tcl_Interp * interp)
{
    return Tcldot_Init(interp);
}

int Tcldot_builtin_Init(Tcl_Interp * interp)
{
    return Tcldot_Init(interp);
}
