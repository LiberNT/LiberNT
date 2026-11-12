/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Searching the tree the firmware describes the machine with
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * Whether one node is the component the caller described. A key of NULL means
 * any key will do, which is how a caller asks for the first device of a kind
 * without knowing how the firmware numbered it.
 */
FORCEINLINE
BOOLEAN
KiIsConfigurationMatch(
    _In_ PCONFIGURATION_COMPONENT_DATA Node,
    _In_ CONFIGURATION_CLASS Class,
    _In_ CONFIGURATION_TYPE Type,
    _In_opt_ PULONG ComponentKey)
{
    PCONFIGURATION_COMPONENT Component = &Node->ComponentEntry;

    if (Component->Class != Class)
        return FALSE;

    if (Component->Type != Type)
        return FALSE;

    if (ComponentKey == NULL)
        return TRUE;

    return Component->Key == *ComponentKey;
}

/*
 * The node that comes after this one in a depth-first walk of the whole tree,
 * without going back above where the walk started.
 */
static
PCONFIGURATION_COMPONENT_DATA
KiNextConfigurationNode(
    _In_ PCONFIGURATION_COMPONENT_DATA Node,
    _In_ PCONFIGURATION_COMPONENT_DATA Root)
{
    if (Node->Child != NULL)
        return Node->Child;

    /*
     * No children: the next node is this one's sibling, or the sibling of the
     * nearest ancestor that has one. Climbing past the root ends the walk.
     */
    while (Node != NULL)
    {
        if (Node == Root)
            return NULL;

        if (Node->Sibling != NULL)
            return Node->Sibling;

        Node = Node->Parent;
    }

    return NULL;
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Carries on a search of the configuration tree from where the last one left
 * off, so that a caller can walk every match rather than only find the first.
 *
 * NextLink holds the place. It is NULL to start a search from Child, and on
 * return it names where to resume; a search that found nothing leaves it NULL.
 */
PCONFIGURATION_COMPONENT_DATA
NTAPI
KeFindConfigurationNextEntry(
    _In_ PCONFIGURATION_COMPONENT_DATA Child,
    _In_ CONFIGURATION_CLASS Class,
    _In_ CONFIGURATION_TYPE Type,
    _In_opt_ PULONG ComponentKey,
    _Inout_ PCONFIGURATION_COMPONENT_DATA *NextLink)
{
    PCONFIGURATION_COMPONENT_DATA Node;

    if (Child == NULL)
        return NULL;

    /* Resuming starts at the node after the one that was returned */
    if (*NextLink != NULL)
        Node = KiNextConfigurationNode(*NextLink, Child);
    else
        Node = Child;

    while (Node != NULL)
    {
        if (KiIsConfigurationMatch(Node, Class, Type, ComponentKey))
        {
            *NextLink = Node;
            return Node;
        }

        Node = KiNextConfigurationNode(Node, Child);
    }

    *NextLink = NULL;
    return NULL;
}

/*
 * The first component of the given class and type in the tree below Child.
 */
PCONFIGURATION_COMPONENT_DATA
NTAPI
KeFindConfigurationEntry(
    _In_ PCONFIGURATION_COMPONENT_DATA Child,
    _In_ CONFIGURATION_CLASS Class,
    _In_ CONFIGURATION_TYPE Type,
    _In_opt_ PULONG ComponentKey)
{
    PCONFIGURATION_COMPONENT_DATA NextLink = NULL;

    return KeFindConfigurationNextEntry(Child,
                                        Class,
                                        Type,
                                        ComponentKey,
                                        &NextLink);
}

/* EOF */
