#ifndef __QUEUE_H__
#define __QUEUE_H__

#include <malloc.h>

/** @file
 *
 * @brief Implementation inline di una coda (FIFO) di valori di tipo
 * queue_elem_t.
 *
 * All queue operations are declared as static inline, and their
 * implementation is in the header file itself, so to allow the
 * compiler to optimize as much as possible. It is possible to enable
 * or disable various checks on the consistency of each opearation
 * with respect to the internal status of the queue object. This is
 * useful for debugging purposes.
 */

/** Il tipo dei valori accodabili nella coda		*/
typedef long queue_elem_t;

/** Indica la corretta terminazione di una funzione	*/
#define OK 0
/** Indica un errore dovuto a mancanza di memoria	*/
#define E_NO_MEMORY -1

/** Rappresentazione interna di una coda		*/
typedef struct {
  queue_elem_t *data;	/**< Array di elementi accodati		*/
  int num_elements;	/**< Numero di elementi accodati adesso	*/
  int max_num_elements;	/**< Max numero di elementi accodabili	*/
  /** Pos. in data[] del prox elemento da inserire	*/
  int ins_pos;
  /** Pos. in data[] del prox elemento da estrarre	*/
  int del_pos;
} queue_t;

/** Inizializza una coda specificando il max. numero di elementi.
 *
 * @param queue		Puntatore all'oggetto queue_t
 * @param max_num_elems	Max. numero di elementi accodabili
 *
 * @return OK oppure E_NO_MEMORY
 */

static int queue_init(queue_t *queue, int max_num_elems) {
  queue->data = (queue_elem_t *) malloc(sizeof(queue_elem_t) * max_num_elems);
  if (queue->data == 0)
    return E_NO_MEMORY;
  queue->num_elements = 0;
  queue->max_num_elements = max_num_elems;
  queue->ins_pos = queue->del_pos = 0;
  return OK;
}

/** Distrugge una coda e le risorse associate	*/
static inline void queue_cleanup(queue_t *queue) {
  free(queue->data);
}

/** Inserisce un elemento nella coda.
 *
 * Non chiamare questa funzione se la coda non e' stata inizializzata
 * o e' piena.
 */
static void queue_insert(queue_t *queue, queue_elem_t elem) {
  queue->data[queue->ins_pos] = elem;
  queue->ins_pos = (queue->ins_pos + 1) % queue->max_num_elements;
  queue->num_elements++;
}

/** Estrae un elemento dalla coda.
 *
 * Non chiamare questa funzione se la coda non e' stata inizializzata
 * o e' vuota.
 *
 * @return	L'elemento estratto.
 */
static queue_elem_t queue_extract(queue_t *queue) {
 queue_elem_t elem;
 elem = queue->data[queue->del_pos];
 queue->del_pos = (queue->del_pos + 1) % queue->max_num_elements;
 queue->num_elements--;
 return elem;
}

/** Restituisce il numero di elementi attualmente accodati.
 *
 * Non chiamare questa funzione se la coda non e' stata inizializzata.
 *
 * @return	Il numero di elementi attualmente in coda.
 */
static int queue_get_num_elements(queue_t *queue) {
  return queue->num_elements;
}

/** Restituisce il massimo numero di elementi accodabili.
 *
 * Non chiamare questa funzione se la coda non e' stata inizializzata.
 *
 * @return	Il max. numero di elementi accodabili.
 */
static inline int queue_get_max_num_elements(queue_t *queue) {
  return queue->max_num_elements;
}

/** Controlla se la coda e' vuota.
 *
 * Non chiamare questa funzione se la coda non e' stata inizializzata.
 *
 * @return	1 se la coda e' vuota, 0 altrimenti.
 */
static inline int queue_is_empty(queue_t *queue) {
  return (queue->num_elements == 0 ? 1 : 0);
}

/** Controlla se la coda e' piena.
 *
 * Non chiamare questa funzione se la coda non e' stata inizializzata.
 *
 * @return	1 se la coda e' piena, 0 altrimenti.
 */
static inline int queue_is_full(queue_t *queue) {
  return (queue->num_elements == queue->max_num_elements ? 1 : 0);
}

typedef struct {
  queue_t *queue;	/**< The queue_t over which we are iterating */
  int pos;		/**< when pos == queue->ins_pos, the position
			 * is past the end */
} queue_iterator_t;

/** Get an iterator positioned on the first (earliest inserted)
 *  element of the queue */
static queue_iterator_t queue_begin(queue_t *this) {
  queue_iterator_t qit = {
    .queue = this,
    .pos = this->del_pos
  };
  return qit;
}

/** Check if we may call queue_it_next() once again */
static int queue_it_has_next(queue_iterator_t *this) {
  return (this->pos != this->queue->ins_pos);
}

/** Retrieve (without removing) next element from the queue */
static queue_elem_t queue_it_next(queue_iterator_t *this) {
  queue_elem_t value = this->queue->data[this->pos];
  this->pos = (this->pos + 1) % this->queue->max_num_elements;
  return value;
}

#endif
