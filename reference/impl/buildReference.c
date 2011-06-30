#include "cactus.h"
#include "sonLib.h"
#include "cactusCycleConstrainedMatchingAlgorithms.h"
#include "cactusMatchingAlgorithms.h"
#include "shared.h"

const char *REFERENCE_BUILDING_EXCEPTION = "REFERENCE_BUILDING_EXCEPTION";

////////////////////////////////////
////////////////////////////////////
//Get the reference event
////////////////////////////////////
////////////////////////////////////

static Event *getReferenceEvent(Flower *flower,
        const char *referenceEventHeader) {
    /*
     * Create the reference event, with the given header, for the flower.
     */
    EventTree *eventTree = flower_getEventTree(flower);
    Event *referenceEvent = eventTree_getEventByHeader(eventTree,
            referenceEventHeader);
    if (referenceEvent == NULL) {
        Group *parentGroup = flower_getParentGroup(flower);
        if (parentGroup == NULL) {
            //We are the root, so make a new event..
            return event_construct3(referenceEventHeader, INT32_MAX,
                    eventTree_getRootEvent(eventTree), eventTree);
        } else {
            Event *parentEvent = eventTree_getEventByHeader(
                    flower_getEventTree(group_getFlower(parentGroup)),
                    referenceEventHeader);
            assert(parentEvent != NULL);
            Event *event = event_construct(event_getName(parentEvent),
                    referenceEventHeader, INT32_MAX,
                    eventTree_getRootEvent(eventTree), eventTree);
            assert(event_getName(event) == event_getName(parentEvent));
            return event;
        }
    }
    return referenceEvent;
}

////////////////////////////////////
////////////////////////////////////
//Get ends from parent
////////////////////////////////////
////////////////////////////////////

static stList *getExtraAttachedStubsFromParent(Flower *flower) {
    /*
     * Copy any stubs not present in the flower from the parent group.
     * At the end there will be an even number of attached stubs in the problem.
     * Returns the list of new ends, which need to be assigned a group.
     */
    Group *parentGroup = flower_getParentGroup(flower);
    stList *newEnds = stList_construct();
    if (parentGroup != NULL) {
        Group_EndIterator *parentEndIt = group_getEndIterator(parentGroup);
        End *parentEnd;
        while ((parentEnd = group_getNextEnd(parentEndIt)) != NULL) {
            if (end_isAttached(parentEnd) || end_isBlockEnd(parentEnd)) {
                End *end = flower_getEnd(flower, end_getName(parentEnd));
                if (end == NULL) { //We have found an end that needs to be pushed into the child.
                    stList_append(newEnds, end_copyConstruct(parentEnd, flower)); //At this point it has no associated group;
                }
            }
        }
        group_destructEndIterator(parentEndIt);
    }
    assert(flower_getAttachedStubEndNumber(flower) % 2 == 0);
    assert(flower_getAttachedStubEndNumber(flower) > 0);
    return newEnds;
}

////////////////////////////////////
////////////////////////////////////
//Functions for creating a map between integers representing the nodes of the graph and
//the ends in the flower
////////////////////////////////////
////////////////////////////////////

static stHash *getMapOfTangleEndsToNodes(Flower *flower) {
    /*
     * Iterates over new ends (from the parent, without a group, and those ends in tangles).
     */
    stHash *endsToNodes = stHash_construct2(NULL,
            (void(*)(void *)) stIntTuple_destruct);
    int32_t endCount = 0;
    Flower_EndIterator *endIt = flower_getEndIterator(flower);
    End *end;
    while ((end = flower_getNextEnd(endIt)) != NULL) {
        Group *group = end_getGroup(end);
        if (group != NULL) {
            if (group_isTangle(group)) {
                if (end_isAttached(end) || end_isBlockEnd(end)) {
                    stHash_insert(endsToNodes, end,
                            stIntTuple_construct(1, endCount++));
                }
            } else {
                assert(group_isLink(group));
            }
        } else {
            assert(end_isStubEnd(end));
            assert(end_isAttached(end));
            stHash_insert(endsToNodes, end, stIntTuple_construct(1, endCount++));
        }
    }
    flower_destructEndIterator(endIt);
    return endsToNodes;
}

////////////////////////////////////
////////////////////////////////////
//Chain edges
////////////////////////////////////
////////////////////////////////////

static stIntTuple *getEdge2(int32_t node1, int32_t node2) {
    assert(node1 != node2);
    if (node1 > node2) {
        return getEdge2(node2, node1);
    }
    return stIntTuple_construct(2, node1, node2);
}

static stIntTuple *getEdge(End *end1, End *end2, stHash *endsToNodes) {
    assert(stHash_search(endsToNodes, end1) != NULL);
    assert(stHash_search(endsToNodes, end2) != NULL);
    return getEdge2(
            stIntTuple_getPosition(stHash_search(endsToNodes, end1), 0),
            stIntTuple_getPosition(stHash_search(endsToNodes, end2), 0));
}

static stIntTuple *getWeightedEdge2(int32_t node1, int32_t node2, int32_t weight) {
    assert(node1 != node2);
    if (node1 > node2) {
        return getWeightedEdge2(node2, node1, weight);
    }
    return stIntTuple_construct(3, node1, node2, weight);
}

static stIntTuple *getWeightedEdge(End *end1, End *end2, int32_t weight, stHash *endsToNodes) {
    assert(stHash_search(endsToNodes, end1) != NULL);
    assert(stHash_search(endsToNodes, end2) != NULL);
    return getWeightedEdge2(
            stIntTuple_getPosition(stHash_search(endsToNodes, end1), 0),
            stIntTuple_getPosition(stHash_search(endsToNodes, end2), 0), weight);
}

static void getNonTrivialChainEdges(Flower *flower, stHash *endsToNodes,
        stList *chainEdges) {
    /*
     * Get the non-trivial chain edges for the flower.
     */
    Flower_ChainIterator *chainIt = flower_getChainIterator(flower);
    Chain *chain;
    while ((chain = flower_getNextChain(chainIt)) != NULL) {
        End *_5End = link_get3End(chain_getFirst(chain));
        End *_3End = link_get5End(chain_getLast(chain));
        if (end_isBlockEnd(_5End) && end_isBlockEnd(_3End)) {
            End *end1 = end_getOtherBlockEnd(_5End);
            End *end2 = end_getOtherBlockEnd(_3End);

            assert(end_getGroup(end1) != NULL);
            assert(end_getGroup(end2) != NULL);
            assert(group_isTangle(end_getGroup(end1)));
            assert(group_isTangle(end_getGroup(end2)));

            assert(stHash_search(endsToNodes, end1) != NULL);
            assert(stHash_search(endsToNodes, end2) != NULL);

            stList_append(chainEdges, getWeightedEdge(end1, end2, chain_getAverageInstanceBaseLength(chain), endsToNodes));
        }
    }
    flower_destructChainIterator(chainIt);
}

static void getTrivialChainEdges(Flower *flower, stHash *endsToNodes,
        stList *chainEdges) {
    /*
     * Get the trivial chain edges for the flower.
     */
    Flower_BlockIterator *blockIt = flower_getBlockIterator(flower);
    Block *block;
    while ((block = flower_getNextBlock(blockIt)) != NULL) {
        End *_5End = block_get5End(block);
        End *_3End = block_get3End(block);
        assert(end_getGroup(_5End) != NULL);
        assert(end_getGroup(_3End) != NULL);
        if (group_isTangle(end_getGroup(_5End)) && group_isTangle(
                end_getGroup(_3End))) {
            stList_append(chainEdges, getWeightedEdge(_5End, _3End, block_getLength(block), endsToNodes));
        }
    }
    flower_destructBlockIterator(blockIt);
}

static stList *getChainEdges(Flower *flower, stHash *endsToNodes) {
    /*
     * Get the trivial chain edges for the flower.
     */
    stList *chainEdges = stList_construct3(0,
            (void(*)(void *)) stIntTuple_destruct);
    getNonTrivialChainEdges(flower, endsToNodes, chainEdges);
    getTrivialChainEdges(flower, endsToNodes, chainEdges);
    return chainEdges;
}

////////////////////////////////////
////////////////////////////////////
//Stub edges
////////////////////////////////////
////////////////////////////////////

static Cap *getCapWithEvent(End *end, Name eventName) {
    /*
     * Get the first encountered cap in the end with the given event.
     */
    Cap *cap;
    End_InstanceIterator *instanceIt = end_getInstanceIterator(end);
    while ((cap = end_getNext(instanceIt)) != NULL) {
        if (event_getName(cap_getEvent(cap)) == eventName) {
            end_destructInstanceIterator(instanceIt);
            return cap;
        }
    }
    end_destructInstanceIterator(instanceIt);
    return NULL;
}

static End *getAdjacentEndFromParent(End *end, Event *referenceEvent) {
    /*
     * Get the adjacent stub end by looking at the reference adjacency in the parent.
     */
    Flower *flower = end_getFlower(end);
    Group *parentGroup = flower_getParentGroup(flower);
    assert(parentGroup != NULL);
    End *parentEnd = group_getEnd(parentGroup, end_getName(end));
    assert(parentEnd != NULL);
    //Now trace the parent end..
    Cap *cap = getCapWithEvent(parentEnd, event_getName(referenceEvent));
    assert(cap != NULL);
    Cap *adjacentCap = cap_getAdjacency(cap);
    assert(adjacentCap != NULL);
    End *adjacentParentEnd = cap_getEnd(adjacentCap);
    End *adjacentEnd = flower_getEnd(flower, end_getName(adjacentParentEnd));
    assert(adjacentEnd != NULL);
    return adjacentEnd;
}

static stList *getStubEdgesFromParent(Flower *flower, stHash *endsToNodes,
        Event *referenceEvent) {
    /*
     * For each attached stub in the flower, get the end in the parent group, and add its
     * adjacency to the group.
     */
    stList *stubEdges = stList_construct3(0,
            (void(*)(void *)) stIntTuple_destruct);
    stList *ends = stHash_getKeys(endsToNodes);
    stSortedSet *endsSeen = stSortedSet_construct();
    for (int32_t i = 0; i < stList_length(ends); i++) {
        End *end = stList_get(ends, i);
        assert(end_getOrientation(end));
        if (end_isStubEnd(end) && stSortedSet_search(endsSeen, end) == NULL) {
            End *adjacentEnd = getAdjacentEndFromParent(end, referenceEvent);
            assert(end_getOrientation(adjacentEnd));

            stSortedSet_insert(endsSeen, end);
            stSortedSet_insert(endsSeen, adjacentEnd);
            stList_append(stubEdges, getEdge(end, adjacentEnd, endsToNodes));
        }
    }
    stSortedSet_destruct(endsSeen);
    stList_destruct(ends);
    return stubEdges;
}

static void getArbitraryStubEdgesP(stSortedSet *nodesSet, int32_t node) {
    stIntTuple *i = stIntTuple_construct(1, node);
    if (stSortedSet_search(nodesSet, i) != NULL) {
        stSortedSet_remove(nodesSet, i);
    }
    stIntTuple_destruct(i);
}

static stList *getArbitraryStubEdges(stHash *endsToNodes, stList *chainEdges) {
    /*
     * Make a set of stub edges, pairing the 'stub ends' arbitrarily.
     */
    stList *nodes = stHash_getValues(endsToNodes);
    stSortedSet *nodesSet = stList_getSortedSet(nodes,
            (int(*)(const void *, const void *)) stIntTuple_cmpFn);
    stList_destruct(nodes);

    /*
     * Filter the set of nodes for those that are part of chain edges.
     */
    for (int32_t i = 0; i < stList_length(chainEdges); i++) {
        stIntTuple *edge = stList_get(chainEdges, i);
        getArbitraryStubEdgesP(nodesSet, stIntTuple_getPosition(edge, 0));
        getArbitraryStubEdgesP(nodesSet, stIntTuple_getPosition(edge, 1));
    }

    assert(stSortedSet_size(nodesSet) % 2 == 0);

    /*
     * The remaining set of edges are 'stubs', create an arbitrary pairing and return it.
     */
    nodes = stSortedSet_getList(nodesSet);
    stList *stubEdges = stList_construct3(0,
            (void(*)(void *)) stIntTuple_destruct);
    for (int32_t i = 0; i < stList_length(nodes); i += 2) {
        stList_append(
                stubEdges,
                getEdge2(stIntTuple_getPosition(stList_get(nodes, i), 0),
                        stIntTuple_getPosition(stList_get(nodes, i + 1), 0)));
    }

    stSortedSet_destruct(nodesSet);
    stList_destruct(nodes);

    return stubEdges;
}

////////////////////////////////////
////////////////////////////////////
//Adjacency edges
////////////////////////////////////
////////////////////////////////////

static End *traceAdjacency(Cap *cap, stSortedSet *activeEnds) {
    while (1) {
        cap = cap_getAdjacency(cap);
        assert(cap != NULL);
        End *end = end_getPositiveOrientation(cap_getEnd(cap));
        if (stSortedSet_search(activeEnds, end) != NULL) {
            return end;
        }
        if (end_isStubEnd(cap_getEnd(cap))) {
            assert(end_isFree(cap_getEnd(cap)));
            return NULL;
        }
        cap = cap_getOtherSegmentCap(cap);
        assert(cap != NULL);
    }
}

static void getAdjacencyEdgesP(End *end, stSortedSet *activeEnds,
        stHash *endsToNodes, stList *adjacencyEdges) {
    /*
     * This function adds adjacencies
     */
    End_InstanceIterator *instanceIt = end_getInstanceIterator(end);
    Cap *cap;
    assert(stHash_search(endsToNodes, end) != NULL);
    assert(end_isAttached(end) || end_isBlockEnd(end));
    while ((cap = end_getNext(instanceIt)) != NULL) {
        if (cap_getSequence(cap) != NULL) {
            cap = cap_getStrand(cap) ? cap : cap_getReverse(cap);
            if (cap_getSide(cap)) { //Use this side property to avoid adding edges twice.
                End *adjacentEnd = traceAdjacency(cap, activeEnds);
                if (adjacentEnd != NULL && adjacentEnd != end) {
                    assert(
                            end_isAttached(adjacentEnd) || end_isBlockEnd(
                                    adjacentEnd));
                    assert(end_getGroup(end) != NULL);
                    assert(end_getGroup(adjacentEnd) != NULL);
                    assert(group_isTangle(end_getGroup(end)));
                    assert(group_isTangle(end_getGroup(adjacentEnd)));
                    assert(stHash_search(endsToNodes, adjacentEnd) != NULL);
                    stList_append(adjacencyEdges,
                            getEdge(end, adjacentEnd, endsToNodes));
                }
            }
        }
    }
    end_destructInstanceIterator(instanceIt);
}

static stList *getAdjacencyEdges(Flower *flower, stSortedSet *activeEnds,
        stHash *endsToNodes) {
    /*
     * Get the adjacency edges for the flower.
     */

    /*
     * First build a list of adjacencies from the tangle ends.
     */
    stList *adjacencyEdges = stList_construct3(0,
            (void(*)(void *)) stIntTuple_destruct);
    stSortedSetIterator *endIt = stSortedSet_getIterator(activeEnds);
    End *end;
    while ((end = stSortedSet_getNext(endIt)) != NULL) {
        getAdjacencyEdgesP(end, activeEnds, endsToNodes, adjacencyEdges);
    }
    stSortedSet_destructIterator(endIt);

    /*
     * Sort them.
     */
    stList_sort(adjacencyEdges,
            (int(*)(const void *, const void *)) stIntTuple_cmpFn);

    /*
     * Iterate over the list, building weighted edges according to the number of duplicates.
     */
    stList *weightedAdjacencyEdges = stList_construct3(0,
            (void(*)(void *)) stIntTuple_destruct);
    int32_t weight = 0;
    stIntTuple *edge = NULL;
    while (stList_length(adjacencyEdges) > 0) {
        stIntTuple *edge2 = stList_pop(adjacencyEdges);
        if (edge != NULL && stIntTuple_cmpFn(edge, edge2) == 0) {
            weight++;
            stIntTuple_destruct(edge2);
        } else {
            if (edge != NULL) {
                assert(weight >= 1);
                stList_append(
                        weightedAdjacencyEdges,
                        stIntTuple_construct(3,
                                stIntTuple_getPosition(edge, 0),
                                stIntTuple_getPosition(edge, 1), weight));
                stIntTuple_destruct(edge);
            }
            edge = edge2;
            weight = 1;
        }
    }
    if (edge != NULL) {
        assert(weight >= 1);
        stList_append(
                weightedAdjacencyEdges,
                stIntTuple_construct(3, stIntTuple_getPosition(edge, 0),
                        stIntTuple_getPosition(edge, 1), weight));
        stIntTuple_destruct(edge);
    }
    stList_destruct(adjacencyEdges);

    return weightedAdjacencyEdges;
}

////////////////////////////////////
////////////////////////////////////
//Functions to add adjacencies and segments, given the chosen edges
////////////////////////////////////
////////////////////////////////////

End *getEndFromNode(stHash *nodesToEnds, int32_t node) {
    /*
     * Get the end for the given node.
     */
    stIntTuple *i = stIntTuple_construct(1, node);
    End *end = stHash_search(nodesToEnds, i);
    assert(end != NULL);
    assert(end_getOrientation(end));
    stIntTuple_destruct(i);
    return end;
}

static Cap *makeCapWithEvent(End *end, Event *referenceEvent) {
    /*
     * Returns a cap in the end that is part of the given reference event.
     */
    Cap *cap = getCapWithEvent(end, event_getName(referenceEvent));
    if (cap == NULL) {
        if (end_isBlockEnd(end)) {
            Block *block = end_getBlock(end);
            Segment *segment = segment_construct(block, referenceEvent);
            if (block_getRootInstance(block) != NULL) {
                segment_makeParentAndChild(block_getRootInstance(block),
                        segment);
            }
            cap = getCapWithEvent(end, event_getName(referenceEvent));
        } else { //Look in parent problem for event
            assert(end_isAttached(end));
            Group *parentGroup = flower_getParentGroup(end_getFlower(end));
            if (parentGroup != NULL) {
                End *parentEnd = group_getEnd(parentGroup, end_getName(end));
                assert(parentEnd != NULL);
                Cap *parentCap = getCapWithEvent(parentEnd,
                        event_getName(referenceEvent));
                assert(parentCap != NULL);
                cap = cap_copyConstruct(end, parentCap);
            } else {
                cap = cap_construct(end, referenceEvent);
            }
            if (end_getRootInstance(end) != NULL) {
                cap_makeParentAndChild(end_getRootInstance(end), cap);
            }
        }
    }
    assert(cap != NULL);
    assert(getCapWithEvent(end, event_getName(referenceEvent)) != NULL);
    return cap;
}

static void addAdjacenciesAndSegmentsP(End *end1, End *end2,
        Event *referenceEvent) {
    Cap *cap1 = makeCapWithEvent(end1, referenceEvent);
    Cap *cap2 = makeCapWithEvent(end2, referenceEvent);
    assert(cap_getAdjacency(cap1) == NULL);
    assert(cap_getAdjacency(cap2) == NULL);
    cap_makeAdjacent(cap1, cap2);
}

static void addLinkAdjacenciesAndSegments(Flower *flower, Event *referenceEvent) {
    Flower_GroupIterator *groupIt = flower_getGroupIterator(flower);
    Group *group;
    while ((group = flower_getNextGroup(groupIt)) != NULL) {
        if (group_isLink(group)) {
            Link *link = group_getLink(group);
            addAdjacenciesAndSegmentsP(link_get3End(link), link_get5End(link),
                    referenceEvent);
        }
    }
    flower_destructGroupIterator(groupIt);
}

static void addTangleAdjacenciesAndSegments(Flower *flower,
        stList *chosenAdjacencyEdges, stHash *nodesToEnds,
        Event *referenceEvent) {
    /*
     * For each chosen adjacency edge ensure there exists a reference cap in each incident end (adding segments as needed),
     * then create the an adjacency for each cap. Where adjacencies link ends in different groups we add extra blocks to 'bridge' the
     * groups.
     */
    for (int32_t i = 0; i < stList_length(chosenAdjacencyEdges); i++) {
        stIntTuple *edge = stList_get(chosenAdjacencyEdges, i);
        End *end1 =
                getEndFromNode(nodesToEnds, stIntTuple_getPosition(edge, 0));
        End *end2 =
                getEndFromNode(nodesToEnds, stIntTuple_getPosition(edge, 1));
        assert(end1 != end2);
        if (end_getGroup(end1) != NULL && end_getGroup(end2) != NULL
                && end_getGroup(end1) != end_getGroup(end2)) {
            /*
             * We build a 'bridging block'.
             */
            Block *block = block_construct(1, flower);
            if (flower_builtTrees(flower)) { //Add a root segment
                Event *event = eventTree_getRootEvent(
                        flower_getEventTree(flower));
                assert(event != NULL);
                block_setRootInstance(block, segment_construct(block, event));
            }

            end_setGroup(block_get5End(block), end_getGroup(end1));
            assert(group_isTangle(end_getGroup(end1)));
            addAdjacenciesAndSegmentsP(end1, block_get5End(block),
                    referenceEvent);

            end_setGroup(block_get3End(block), end_getGroup(end2));
            assert(group_isTangle(end_getGroup(end2)));
            addAdjacenciesAndSegmentsP(end2, block_get3End(block),
                    referenceEvent);
        } else {
            addAdjacenciesAndSegmentsP(end1, end2, referenceEvent);
        }
    }
}

////////////////////////////////////
////////////////////////////////////
//Put the new ends into groups.
////////////////////////////////////
////////////////////////////////////

static void assignGroups(stList *newEnds, Flower *flower, Event *referenceEvent) {
    /*
     * Put ends into groups.
     */
    for (int32_t i = 0; i < stList_length(newEnds); i++) {
        End *end = stList_get(newEnds, i);
        if (end_getGroup(end) == NULL) { //It may already have been assigned.
            Cap *cap = getCapWithEvent(end, event_getName(referenceEvent));
            assert(cap != NULL);
            Cap *adjacentCap = cap_getAdjacency(cap);
            assert(adjacentCap != NULL);
            End *adjacentEnd = cap_getEnd(adjacentCap);
            Group *group = end_getGroup(adjacentEnd);
            if (group == NULL) {
                /*
                 * Two new ends link to one another, hence we need to find an existing tangle group.
                 */
                Flower_GroupIterator *groupIt = flower_getGroupIterator(flower);
                while ((group = flower_getNextGroup(groupIt)) != NULL) {
                    if (group_isTangle(group)) {
                        break;
                    }
                }
                assert(group_isTangle(group));
                flower_destructGroupIterator(groupIt);
                end_setGroup(adjacentEnd, group);
            }
            end_setGroup(end, group);
        }
    }
}

////////////////////////////////////
////////////////////////////////////
//Main function
////////////////////////////////////
////////////////////////////////////

stHash *getNodesToEdgesHash(stList *edges);

stIntTuple *getEdgeForNodes(int32_t node1, int32_t node2,
        stHash *nodesToAdjacencyEdges);

void makeEdgesAClique(stList *edges, stSortedSet *nodes, int32_t defaultWeight) {
    /*
     * Adds edges to the list to make the set of edges a clique.
     */
    stHash *nodesToEdges = getNodesToEdgesHash(edges);
    stSortedSetIterator *nodeIt = stSortedSet_getIterator(nodes);
    stIntTuple *node;
    while ((node = stSortedSet_getNext(nodeIt)) != NULL) {
        int32_t i = stIntTuple_getPosition(node, 0);
        stSortedSetIterator *nodeIt2 = stSortedSet_getIteratorFrom(nodes, node);
        stIntTuple *node2 = stSortedSet_getNext(nodeIt2);
        assert(node == node2);
        while ((node2 = stSortedSet_getNext(nodeIt2)) != NULL) {
            assert(node2 != node);
            int32_t j = stIntTuple_getPosition(node2, 0);
            assert(i < j);
            if (getEdgeForNodes(i, j, nodesToEdges) == NULL) {
                stList_append(edges,
                        stIntTuple_construct(3, i, j, defaultWeight));
            }
        }
        stSortedSet_destructIterator(nodeIt2);
    }
    stSortedSet_destructIterator(nodeIt);
    stHash_destruct(nodesToEdges);
}

static int compareEdgesByWeight(const void *edge, const void *edge2) {
    int32_t i = stIntTuple_getPosition((stIntTuple *) edge, 2);
    int32_t j = stIntTuple_getPosition((stIntTuple *) edge2, 2);
    return i > j ? 1 : (i < j ? -1 : 0);
}

stSortedSet *getEndsFromNodes(stSortedSet *nodes, stHash *nodesToEnds) {
    stSortedSet *ends = stSortedSet_construct();
    stSortedSetIterator *it = stSortedSet_getIterator(nodes);
    stIntTuple *node;
    while ((node = stSortedSet_getNext(it)) != NULL) {
        End *end = stHash_search(nodesToEnds, node);
        assert(end != NULL);
        stSortedSet_insert(ends, end);
    }
    stSortedSet_destructIterator(it);
    return ends;
}

void buildReferenceTopDown(Flower *flower, const char *referenceEventHeader, int32_t maxNumberOfChainsToSolvePerRound,
        stList *(*matchingAlgorithm)(stList *edges, int32_t nodeNumber)) {
    /*
     * Implements the following pseudo code.
     *
     * S = initial constraints
     * C = chains
     * G = adjacency graph
     * X = integer > 0
     * def layeredMatching(C, S, G, X):
     *      while C not empty:
     *          C' = largest(C, X) #Get largest X chains
     *          N = nodes(C \cup S) #Get nodes at ends of constraint and chain edges
     *          A = adjacencies(N, G) #Get adjacencies between N, as traced through G
     *          S = matching(N, C', S, A) #Calculate the best matching, respecting set of constraints S
     *          C = C \ C' #Update the list of chains left to process
     *          return S #All edges ultimately become stubs
     */

    /*
     * Get the reference event
     */
    Event *referenceEvent = getReferenceEvent(flower, referenceEventHeader);

    /*
     * Get any extra ends to balance the group from the parent problem.
     */
    stList *newEnds = getExtraAttachedStubsFromParent(flower);

    /*
     * Create a map to identify the ends by integers.
     */
    stHash *endsToNodes = getMapOfTangleEndsToNodes(flower);
    stHash *nodesToEnds = stHash_invert(endsToNodes,
            (uint32_t(*)(const void *)) stIntTuple_hashKey,
            (int(*)(const void *, const void *)) stIntTuple_equalsFn, NULL,
            NULL);
    int32_t nodeNumber = stHash_size(endsToNodes);
    assert(nodeNumber % 2 == 0);

    /*
     * Get the chain edges.
     */
    stList *chainEdges = getChainEdges(flower, endsToNodes);
    stList_sort(chainEdges, compareEdgesByWeight); //Sort them add them to the matching progressively

    /*
     * Get the stub edges.
     */
    bool hasParent = flower_getParentGroup(flower) != NULL;
    stList *stubEdges = hasParent ? getStubEdgesFromParent(flower, endsToNodes,
            referenceEvent) : getArbitraryStubEdges(endsToNodes, chainEdges);
    bool makeStubCyclesDisjoint = hasParent;

    while (stList_length(chainEdges) > 0) {
        /*
         * Get the chain edges to add to the matching..
         */
        stList *activeChainEdges = stList_construct();
        int32_t pChainLength = INT32_MAX;
        while (stList_length(chainEdges) > 0 && stList_length(activeChainEdges)
                < maxNumberOfChainsToSolvePerRound) {
            stList_append(activeChainEdges, stList_pop(chainEdges));
            int32_t i = stIntTuple_getPosition(stList_peek(activeChainEdges), 2);
            assert(i <= pChainLength);
            pChainLength = i;
        }

        /*
         * Get the working set of nodes.
         */
        stList *chainAndStubEdges = stList_copy(activeChainEdges, NULL);
        stList_appendAll(chainAndStubEdges, stubEdges);
        stSortedSet *activeNodes = getNodeSetOfEdges(chainAndStubEdges);
        stSortedSet *activeEnds = getEndsFromNodes(activeNodes, nodesToEnds);
        stList_destruct(chainAndStubEdges);

        /*
         * Get the adjacency edges.
         */
        stList *adjacencyEdges = getAdjacencyEdges(flower, activeEnds,
                endsToNodes);
        makeEdgesAClique(adjacencyEdges, activeNodes, 0);

        st_logDebug(
                "Going to build a reference matching for the flower %lli with %i nodes, %i adjacencies with cardinality %i and weight %i,  %i stub edges and %i chain edges, %i attached stubs, %i free stubs and %i block ends,  %i ends total and %i chains\n",
                flower_getName(flower), stSortedSet_size(activeNodes),
                stList_length(adjacencyEdges),
                matchingCardinality(adjacencyEdges),
                matchingWeight(adjacencyEdges), stList_length(stubEdges),
                stList_length(chainEdges),
                flower_getAttachedStubEndNumber(flower),
                flower_getFreeStubEndNumber(flower),
                flower_getBlockEndNumber(flower), flower_getEndNumber(flower),
                flower_getChainNumber(flower));

        /*
         * Calculate the matching
         */
        stList *chosenAdjacencyEdges = getMatchingWithCyclicConstraints(
                activeNodes, adjacencyEdges, stubEdges, activeChainEdges,
                makeStubCyclesDisjoint, matchingAlgorithm);
        makeStubCyclesDisjoint = 1;

        /*
         * Now update the set of stub edges..
         */
        stList_destruct(stubEdges);
        stubEdges = stList_construct3(0, (void(*)(void *)) stIntTuple_destruct);
        while (stList_length(chosenAdjacencyEdges) > 0) {
            stIntTuple *edge = stList_pop(chosenAdjacencyEdges);
            stList_append(
                    stubEdges,
                    getEdge2(stIntTuple_getPosition(edge, 0),
                            stIntTuple_getPosition(edge, 1)));
        }

        /*
         * Cleanup the loop
         */
        stList_destruct(chosenAdjacencyEdges);
        stList_destruct(adjacencyEdges);
        stSortedSet_destruct(activeNodes);
        stSortedSet_destruct(activeEnds);
    }

    /*
     * Add the reference genome into flower
     */
    addLinkAdjacenciesAndSegments(flower, referenceEvent);
    addTangleAdjacenciesAndSegments(flower, stubEdges, nodesToEnds,
            referenceEvent);

    /*
     * Ensure the newly created ends have a group.
     */
    assignGroups(newEnds, flower, referenceEvent);

    /*
     * Cleanup
     */
    stList_destruct(newEnds);
    stHash_destruct(endsToNodes);
    stHash_destruct(nodesToEnds);
    stList_destruct(chainEdges);
    stList_destruct(stubEdges);
}

