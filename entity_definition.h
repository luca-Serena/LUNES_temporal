/*	##############################################################################################
 *      Advanced RTI System, ARTÌS			http://pads.cs.unibo.it
 *      Large Unstructured NEtwork Simulator (LUNES)
 *
 *      Description:
 *              -	In this file is defined the state of the simulated entitiesFORKI
 *
 *      Authors:
 *              First version by Gabriele D'Angelo <g.dangelo@unibo.it>
 *
 ############################################################################################### */

#ifndef __ENTITY_DEFINITION_H
#define __ENTITY_DEFINITION_H
//#define HIERARCHY

#include "lunes_constants.h"


/*---- E N T I T I E S    D E F I N I T I O N ---------------------------------*/

/*! \brief Structure of "value" in the hash table of each node
 *         in LUNES used to implement neighbors and its properties
 */
typedef struct v_e {
    unsigned int value;                     // Value
} value_element;

/*! \brief Records composing the local state (dynamic part) of each SE
 *         NOTE: no duplicated keys are allowed
 */
struct state_element {
    unsigned int  key;      // Key
    value_element elements; // Value
};

/*! \brief SE state definition */
typedef struct hash_data_t {
    int           key;                    // SE identifier
    int           lp;                     // Logical Process ID (that is the SE container)
    int           internal_timer;         // Used to track mining activity
    int	      	  status;	    // 0 off 1 active 2 applicant 3 holder 5 active and received the message 4 holder and received the message
    GHashTable *  state;                  // Local state as an hash table (glib) (dynamic part)
    int		  received;	    // 0 not received anything, !=0 received the message, -1 received the message back in the fluff phase
    unsigned int  num_neighbors;           // Number of SE's neighbors (dynamically updated)
    //#endif
} hash_data_t;

#endif /* __ENTITY_DEFINITION_H */
