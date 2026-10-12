/* Behavioral tests of the production exact BSP traversal. */
#include "../common/world_solid_interval.h"
#include <assert.h>
#include <stdio.h>

typedef struct test_tree_s {
    wbsp_node_t nodes[300];
    int count;
} test_tree_t;
static int read_node(void *opaque, int index, wbsp_node_t *out) {
    test_tree_t *tree = opaque;
    if (index < 0 || index >= tree->count) return 0;
    *out = tree->nodes[index];
    return 1;
}
static void plane(test_tree_t *tree, int index, double x, int front, int back) {
    tree->nodes[index].normal[0] = 1;
    tree->nodes[index].distance = x;
    tree->nodes[index].children[0] = front;
    tree->nodes[index].children[1] = back;
    if (index >= tree->count) tree->count = index + 1;
}
static int query(test_tree_t *tree, double start, double direction, double length,
    wbsp_interval_t *out) {
    double origin[3] = {start,0,0}, ray[3] = {direction,0,0};
    return WBSP_FirstSolidInterval(tree,read_node,0,origin,ray,length,out);
}
static void near(double a,double b) { assert(fabs(a-b)<1e-9); }

int main(void) {
    test_tree_t tree = {{{{0,0,0},0,{0,0}}},0};
    wbsp_interval_t result;
    int i;
    plane(&tree,0,0,1,-1);
    plane(&tree,1,2.08,2,-2);
    plane(&tree,2,2.18,3,-1);
    plane(&tree,3,4,-1,-2);
    assert(query(&tree,-1,1,100,&result));
    near(result.begin,1); near(result.end,3.08); near(result.clear_end,3.18);
    assert(result.entry_node==0 && result.entry_side==0);
    assert(result.exit_node==1 && result.exit_side==1);
    assert(query(&tree,5,-1,100,&result));
    near(result.begin,1); near(result.end,2.82); near(result.clear_end,2.92);
    assert(result.entry_node==3 && result.entry_side==1);
    assert(result.exit_node==2 && result.exit_side==0);
    /* Gap phases anywhere between the old 0.25-unit samples stay separate. */
    for (i=1;i<=50;++i) {
        double gap=0.001*i;
        tree.nodes[2].distance=2.08+gap;
        assert(query(&tree,-1,1,100,&result));
        near(result.end,3.08); near(result.clear_end,3.08+gap);
    }
    /* A second wall fully inside the old exit offset is not cleared. */
    tree.nodes[2].distance=2.18;
    tree.nodes[3].distance=2.22;
    assert(query(&tree,-1,1,100,&result));
    near(result.end,3.08); near(result.clear_end,3.18);
    /* Touching solids form one connected interval, not a fictitious gap. */
    tree.nodes[2].distance=2.08;
    tree.nodes[3].distance=4;
    assert(query(&tree,-1,1,100,&result));
    near(result.end,5);
    /* Widely separated solids and a standalone wall. */
    tree.nodes[2].distance=20; tree.nodes[3].distance=22;
    assert(query(&tree,-1,1,100,&result));
    near(result.end,3.08); near(result.clear_end,21);
    tree.nodes[1].children[0]=-1;
    assert(query(&tree,-1,1,10,&result));
    near(result.end,3.08); near(result.clear_end,10);
    /* Thin geometry is identified without a first point sample inside it. */
    tree.nodes[1].distance=0.02;
    assert(query(&tree,-1,1,10,&result)); near(result.end,1.02);
    assert(!query(&tree,0.01,1,10,&result)); /* start solid */
    assert(!query(&tree,-1,1,0.5,&result)); /* no solid */
    assert(!query(&tree,-1,1,1.01,&result)); /* no exit */
    assert(!query(&tree,-1,1,NAN,&result));
    assert(!query(&tree,-1,2,10,&result));
    tree.nodes[0].children[0]=900; assert(!query(&tree,-1,1,10,&result));
    tree.nodes[0].children[0]=0; assert(!query(&tree,-1,1,10,&result)); /* cycle */
    tree.nodes[0].normal[0]=NAN; assert(!query(&tree,-1,1,10,&result));
    /* A malformed deep tree is bounded rather than recursing indefinitely. */
    memset(&tree,0,sizeof(tree));
    for(i=0;i<299;++i) plane(&tree,i,-10,i+1,-1);
    plane(&tree,299,0,-1,-2);
    assert(!query(&tree,1,1,10,&result));
    puts("PASS J.10.6.1: exact intervals, narrow/thin/reversed/touching geometry and bounded malformed input");
    return 0;
}
