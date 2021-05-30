#include <ctype.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <err.h>
#include <getopt.h>
#include <sys/time.h>

#include <mpi.h>
#include <unistd.h>

double start = 0.0;
double timeOffset = 0.0;

char *in_filename = NULL;              // nom du fichier contenant la matrice
bool print_solutions = false;          // affiche chaque solution
long long report_delta = 1e6;          // affiche un rapport tous les ... noeuds
long long next_report;                 // prochain rapport affiché au noeud...
long long max_solutions = 0x7fffffffffffffff;        // stop après ... solutions
char saveFile[101];
char saveFileNewName[101];

int NUMBER_OF_OPTIONS_TO_START = 20;

typedef struct instance_t {
        int n_items;
        int n_primary;
        int n_options;
        char **item_name;   // potentiellement NULL, sinon de taille n_items
        int *options;       // l'option i contient les objets options[ptr[i]:ptr[i+1]]
        int *ptr;           // taille n_options + 1
} instance_t;

typedef struct sparse_array_t {
        int len;           // nombre d'éléments stockés
        int capacity;      // taille maximale
        int *p;            // contenu de l'ensemble = p[0:len] 
        int *q;            // taille capacity (tout comme p)
} sparse_array_t;

typedef struct context_t {
        sparse_array_t *active_items;      // objets actifs
        sparse_array_t **active_options;   // options actives contenant l'objet i
        int *chosen_options;                      // options choisies à ce stade
        int *child_num;                           // numéro du fils exploré
        int *num_children;                        // nombre de fils à explorer
        int level;                                // nombre d'options choisies
        long long nodes;                          // nombre de noeuds explorés
        long long solutions;                      // nombre de solutions trouvées 
} context_t;

static const char DIGITS[62] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
                                'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 
                                'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't',
                                'u', 'v', 'w', 'x', 'y', 'z',
                                'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 
                                'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T',
                                'U', 'V', 'W', 'X', 'Y', 'Z'};


sparse_array_t *copy_sparse_array(const sparse_array_t *source) {
    sparse_array_t *destination = malloc(sizeof(sparse_array_t));
    if (!destination)
        err(1, "Erreur dans l'allocation du copy_array");
    int capacity = source->capacity;

    destination->len = source->len;
    destination->capacity = capacity;

    destination->p = malloc(capacity * sizeof(int));
    destination->q = malloc(capacity * sizeof(int));
    int *p = destination->p;
    int *q = destination->q;
    if (!p || !q)
        err(1, "Erreur dans l'allocation du copy_array, arrays");

    int *srcP = source->p;
    int *srcQ = source->q;
    for (int i = 0 ; i < capacity ; ++i) {
        *p++ = *srcP++;
        *q++ = *srcQ++;
    }
    return destination;
}

void free_sparse_array(sparse_array_t **target) {
    free((*target)->p);
    free((*target)->q);
    free(*target);
}

context_t *copy_context(context_t *source, int n) { /*n = instance->n_items */
    context_t *destination = malloc(sizeof(context_t));
    if (destination == NULL)
        err(1, "impossible d'allouer un contexte");
    destination->level = source->level;
    destination->nodes = source->nodes;
//    destination->solutions = source->solutions;
    destination->solutions = 0;

    destination->active_options = malloc(n * sizeof(*destination->active_options));
    destination->chosen_options = malloc(n * sizeof(*destination->chosen_options));
    destination->child_num      = malloc(n * sizeof(*destination->child_num));
    destination->num_children   = malloc(n * sizeof(*destination->num_children));
    if (!destination->active_options || !destination->chosen_options || !destination->child_num || !destination->num_children)
        err(1, "Erreur dans l'allocation du context_copy");
    
    destination->active_items = copy_sparse_array(source->active_items);
    for (int i = 0 ; i < n ; ++i) {
        destination->active_options[i] = copy_sparse_array(source->active_options[i]);
        destination->chosen_options[i] = source->chosen_options[i];
        destination->child_num[i]      = source->child_num[i];
        destination->num_children[i]   = source->num_children[i];
    }
    return destination;
}

void free_context(context_t **target, int n) {
    for (int i = 0 ; i < n ; ++i)
        free_sparse_array(&((*target)->active_options[i]));
    free((*target)->active_options);
    free((*target)->chosen_options);
    free((*target)->child_num);
    free((*target)->num_children);
    free_sparse_array(&(*target)->active_items);

    free(*target);
}


double wtime() {
    struct timeval ts;
    gettimeofday(&ts, NULL);
    return (double) ts.tv_sec + ts.tv_usec / 1e6;
}


void usage(char **argv) {
    printf("%s --in FILENAME [OPTIONS]\n\n", argv[0]);
    printf("Options:\n");
    printf("--progress-report N   display a message every N nodes (0 to disable)\n");
    printf("--print-solutions     display solutions when they are found\n");
    printf("--stop-after N        stop the search once N solutions are found\n");
}


bool item_is_primary(const instance_t *instance, int item) {
    return item < instance->n_primary;
}


void print_option(const instance_t *instance, int option) {
    if (instance->item_name == NULL)
        errx(1, "tentative d'affichage sans noms d'objet");
    for (int p = instance->ptr[option]; p < instance->ptr[option + 1]; p++) {
        int item = instance->options[p];
        printf("%s ", instance->item_name[item]);
    }
    printf("\n");
}



sparse_array_t * sparse_array_init(int n) {
    sparse_array_t *S = malloc(sizeof(*S));
    if (S == NULL)
        err(1, "impossible d'allouer un tableau creux");
    S->len = 0;
    S->capacity = n;
    S->p = malloc(n * sizeof(int));
    S->q = malloc(n * sizeof(int));
    if (S->p == NULL || S->q == NULL)
        err(1, "Impossible d'allouer p/q dans un tableau creux");
    for (int i = 0; i < n; i++)
        S->q[i] = n;           // initialement vide
    return S;
}

bool sparse_array_membership(const sparse_array_t *S, int x) {
    return (S->q[x] < S->len);
}

bool sparse_array_empty(const sparse_array_t *S) {
    return (S->len == 0);
}

void sparse_array_add(sparse_array_t *S, int x) {
    int i = S->len;
    S->p[i] = x;
    S->q[x] = i;
    ++(S->len);
}

void sparse_array_remove(sparse_array_t *S, int x) {
    int j = S->q[x];
    int n = --(S->len);
//    int n = S->len - 1;
    // échange p[j] et p[n] 
    int y = S->p[n];
    S->p[n] = x;
    S->p[j] = y;
    // met q à jour
    S->q[x] = n;
    S->q[y] = j;
//    S->len = n;
}

void sparse_array_unremove(sparse_array_t *S) {
    ++(S->len);
}

void sparse_array_unadd(sparse_array_t *S) {
    --(S->len);
}


bool item_is_active(const context_t *ctx, int item) {
    return sparse_array_membership(ctx->active_items, item);
}

void solution_found(const instance_t *instance, context_t *ctx) {
    ctx->solutions++;
    if (!print_solutions)
        return;
    printf("Trouvé une nouvelle solution au niveau %d après %lld noeuds\n", ctx->level, ctx->nodes);
    printf("Options : \n");
    for (int i = 0; i < ctx->level; i++) {
        int option = ctx->chosen_options[i];
        printf("+ %d : ", option);
        print_option(instance, option);
    }
    printf("\n----------------------------------------------------\n");
}

void cover(const instance_t *instance, context_t *ctx, int item);

void choose_option(const instance_t *instance, context_t *ctx, int option, int chosen_item) {
    ctx->chosen_options[ctx->level] = option;
    ctx->level++;
    for (int p = instance->ptr[option]; p < instance->ptr[option + 1]; p++) {
        int item = instance->options[p];
        if (item == chosen_item)
            continue;
        cover(instance, ctx, item);
    }
}

void uncover(const instance_t *instance, context_t *ctx, int item);

void unchoose_option(const instance_t *instance, context_t *ctx, int option, int chosen_item) {
    for (int p = instance->ptr[option + 1] - 1; p >= instance->ptr[option]; p--) {
        int item = instance->options[p];
        if (item == chosen_item)
            continue;
        uncover(instance, ctx, item);
    }
    ctx->level--;
}


int choose_next_item(context_t *ctx) {
    int best_item = -1;
    int best_options = 0x7fffffff;
    sparse_array_t *active_items = ctx->active_items;
    for (int i = 0; i < active_items->len; i++) {
        int item = active_items->p[i];
        sparse_array_t *active_options = ctx->active_options[item];
        int k = active_options->len;
        if (k < best_options) {
            best_item = item;
            best_options = k;
        }
    }
    return best_item;
}

void progress_report(const context_t *ctx) {
    double now = wtime();
    printf("Exploré %lld noeuds, trouvé %lld solutions, temps écoulé %.1fs. ", ctx->nodes, ctx->solutions, now - start);
    int i = 0;
    for (int k = 0; k < ctx->level; k++) {
        if (i > 44)
            break;
        int n = ctx->child_num[k];
        int m = ctx->num_children[k];
        if (m == 1)
            continue;
        printf("%c%c ", (n < 62) ? DIGITS[n] : '*', (m < 62) ? DIGITS[m] : '*');
        i++;
    }
    printf("\n"),
    next_report = ctx->nodes + report_delta;
}

void deactivate(const instance_t *instance, context_t *ctx, int option, int covered_item);

void cover(const instance_t *instance, context_t *ctx, int item) {
    if (item_is_primary(instance, item))
        sparse_array_remove(ctx->active_items, item);
    sparse_array_t *active_options = ctx->active_options[item];
    for (int i = 0; i < active_options->len; i++) {
        int option = active_options->p[i];
        deactivate(instance, ctx, option, item);
    }
}


void deactivate(const instance_t *instance, context_t *ctx, int option, int covered_item) {
    for (int k = instance->ptr[option]; k < instance->ptr[option+1]; k++) {
        int item = instance->options[k];
        if (item == covered_item)
            continue;
        sparse_array_remove(ctx->active_options[item], option);
    }
}


void reactivate(const instance_t *instance, context_t *ctx, int option, int uncovered_item);

void uncover(const instance_t *instance, context_t *ctx, int item) {
    sparse_array_t *active_options = ctx->active_options[item];
    for (int i = active_options->len - 1; i >= 0; i--) {
        int option = active_options->p[i];
        reactivate(instance, ctx, option, item);
    }
    if (item_is_primary(instance, item))
        sparse_array_unremove(ctx->active_items);
}


void reactivate(const instance_t *instance, context_t *ctx, int option, int uncovered_item) {
    for (int k = instance->ptr[option + 1] - 1; k >= instance->ptr[option]; k--) {
        int item = instance->options[k];
        if (item == uncovered_item)
            continue;
        sparse_array_unremove(ctx->active_options[item]);
    }
}


instance_t * load_matrix(const char *filename) {
    instance_t *instance = malloc(sizeof(*instance));
    if (instance == NULL)
        err(1, "Impossible d'allouer l'instance");
    FILE *in = fopen(filename, "r");
    if (in == NULL)
        err(1, "Impossible d'ouvrir %s en lecture", filename);
    int n_it, n_op;
    if (fscanf(in, "%d %d\n", &n_it, &n_op) != 2)
        errx(1, "Erreur de lecture de la taille du problème\n");
    if (n_it == 0 || n_op == 0)
        errx(1, "Impossible d'avoir 0 objets ou 0 options");
    instance->n_items = n_it;
    instance->n_primary = 0;
    instance->n_options = n_op;
    instance->item_name = malloc(n_it * sizeof(char *));
    instance->ptr = malloc((n_op + 1) * sizeof(int));
    instance->options = malloc(n_it * n_op *sizeof(int));         // surallocation massive
    if (instance->item_name == NULL || instance->ptr == NULL || instance->options == NULL)
        err(1, "Impossible d'allouer la mémoire pour stocker la matrice");


    enum state_t {START, ID, WHITESPACE, BAR, ENDLINE, ENDFILE};
    enum state_t state = START;

    char buffer[256];
    int i = 0;     // prochain octet disponible du buffer
    int n = 0;     // dernier octet disponible du buffer

    char id[65];
    id[64] = 0;    // sentinelle à la fin, quoi qu'il arrive
    int j = 0;     // longueur de l'identifiant en cours de lecture

    int current_item = 0;
    while (state != ENDLINE) {
        enum state_t prev_state = state;
        if (i >= n) {
            n = fread(buffer, 1, 256, in);
            if (n == 0) {
                if (feof(in)) {
                    state = ENDFILE;
                }
                if (ferror(in))
                    err(1, "erreur lors de la lecture de %s", in_filename);
            }
            i = 0;
        }
        if (state == ENDFILE) {
            // don't examine buffer[i]
        } else if (buffer[i] == '\n') {
            state = ENDLINE;
        } else if (buffer[i] == '|') {
            state = BAR;
        } else if (isspace(buffer[i])) {
            state = WHITESPACE;
        } else {
            state = ID;
        }

        // traite le caractère lu
        if (state == ID) {
            if (j == 64)
                errx(1, "nom d'objet trop long : %s", id);
            id[j] = buffer[i];
            j++;
        }
        if (prev_state == ID && state != ID) {
            id[j] = '\0';
            if (current_item == instance->n_items)
                errx(1, "Objet excedentaire : %s", id);
            for (int k = 0; k < current_item; k++)
                if (strcmp(id, instance->item_name[k]) == 0)
                        errx(1, "Nom d'objets dupliqué : %s", id);
            instance->item_name[current_item] = malloc(j+1);
            strcpy(instance->item_name[current_item], id);
            current_item++;
            j = 0;
        }
        if (state == BAR)
            instance->n_primary = current_item;
        if (state == ENDFILE)
            errx(1, "Fin de fichier prématurée");
        // passe au prochain caractère
        i++;
    }
    if (current_item != instance->n_items)
        errx(1, "Incohérence : %d objets attendus mais seulement %d fournis\n", instance->n_items, current_item);
    if (instance->n_primary == 0)
        instance->n_primary = instance->n_items;

    int current_option = 0;
    int p = 0;       // pointeur courant dans instance->options
    instance->ptr[0] = p;
    bool has_primary = false;
    while (state != ENDFILE) {
        enum state_t prev_state = state;
        if (i >= n) {
            n = fread(buffer, 1, 256, in);
            if (n == 0) {
                if (feof(in))
                    state = ENDFILE;
                if (ferror(in))
                    err(1, "erreur lors de la lecture de %s", in_filename);
            }
            i = 0;
        }
        if (state == ENDFILE) {
            // don't examine buffer[i]
        } else if (buffer[i] == '\n') {
            state = ENDLINE;
        } else if (buffer[i] == '|') {
            state = BAR;
        } else if (isspace(buffer[i])) {
            state = WHITESPACE;
        } else {
            state = ID;
        }

        // traite le caractère lu
        if (state == ID) {
            if (j == 64)
                errx(1, "nom d'objet trop long : %s", id);
            id[j] = buffer[i];
            j++;
        }
        if (prev_state == ID && state != ID) {
            id[j] = '\0';
            // identifie le numéro de l'objet en question
            int item_number = -1;
            for (int k = 0; k < instance->n_items; k++)
                if (strcmp(id, instance->item_name[k]) == 0) {
                    item_number = k;
                    break;
                }
            if (item_number == -1)
                errx(1, "Objet %s inconnu dans l'option #%d", id, current_option);
            // détecte les objets répétés
            for (int k = instance->ptr[current_option]; k < p; k++)
                if (item_number == instance->options[k])
                    errx(1, "Objet %s répété dans l'option %d\n", instance->item_name[item_number], current_option);
            instance->options[p] = item_number;
            p++;
            has_primary |= item_is_primary(instance, item_number);
            j = 0;
        }
        if (state == BAR) {
            errx(1, "Trouvé | dans une option.");
        }
        if ((state == ENDLINE || state == ENDFILE)) {
            // esquive les lignes vides
            if (p > instance->ptr[current_option]) {
                if (current_option == instance->n_options)
                    errx(1, "Option excédentaire");
                if (!has_primary)
                    errx(1, "Option %d sans objet primaire\n", current_option);
                current_option++;
                instance->ptr[current_option] = p;
                has_primary = false;
            }
        }
        // passe au prochain caractère
        i++;
    }
    if (current_option != instance->n_options)
        errx(1, "Incohérence : %d options attendues mais seulement %d fournies\n", instance->n_options, current_option);


    fclose(in);
    fprintf(stderr, "Lu %d objets (%d principaux) et %d options\n", instance->n_items, instance->n_primary, instance->n_options);
    return instance;
}


context_t * backtracking_setup(const instance_t *instance) {
    context_t *ctx = malloc(sizeof(*ctx));
    if (ctx == NULL)
        err(1, "impossible d'allouer un contexte");
    ctx->level = 0;
    ctx->nodes = 0;
    ctx->solutions = 0;
    int n = instance->n_items;
    int m = instance->n_options;
    ctx->active_options = malloc(n * sizeof(*ctx->active_options));
    ctx->chosen_options = malloc(n * sizeof(*ctx->chosen_options));
    ctx->child_num = malloc(n * sizeof(*ctx->child_num));
    ctx->num_children = malloc(n * sizeof(*ctx->num_children));
    if (ctx->active_options == NULL || ctx->chosen_options == NULL || ctx->child_num == NULL || ctx->num_children == NULL)
        err(1, "impossible d'allouer le contexte");
    ctx->active_items = sparse_array_init(n);
    for (int item = 0; item < instance->n_primary; item++)
        sparse_array_add(ctx->active_items, item);

    for (int item = 0; item < n; item++)
        ctx->active_options[item] = sparse_array_init(m);
    for (int option = 0; option < m; option++)
        for (int k = instance->ptr[option]; k < instance->ptr[option + 1]; k++) {
            int item = instance->options[k];
            sparse_array_add(ctx->active_options[item], option);
        }


    return ctx;
}

void solve(const instance_t *instance, context_t *ctx) {
    ctx->nodes++;
    if (ctx->nodes == next_report)
        progress_report(ctx);
    if (sparse_array_empty(ctx->active_items)) {
        solution_found(instance, ctx);
        return;                         /* succès : plus d'objet actif */
    }
    int chosen_item = choose_next_item(ctx);
    sparse_array_t *active_options = ctx->active_options[chosen_item];
    if (sparse_array_empty(active_options))
        return;           /* échec : impossible de couvrir chosen_item */
    cover(instance, ctx, chosen_item);
    ctx->num_children[ctx->level] = active_options->len;
    for (int k = 0; k < active_options->len; k++) {
        int option = active_options->p[k];
        ctx->child_num[ctx->level] = k;
        choose_option(instance, ctx, option, chosen_item);
        solve(instance, ctx);
        if (ctx->solutions >= max_solutions)
            return;
        unchoose_option(instance, ctx, option, chosen_item);
    }

    uncover(instance, ctx, chosen_item);                      /* backtrack */
}

void free_instance(instance_t *instance) {
    for (int i = 0 ; i < instance->n_items ; ++i)
        free(instance->item_name[i]);

    free(instance->item_name);
    free(instance->ptr);
    free(instance->options);

    free(instance);
}

char initiation(int argc, char *argv[]) {
    struct option longopts[6] = {
        {"in", required_argument, NULL, 'i'},
        {"progress-report", required_argument, NULL, 'v'},
        {"print-solutions", no_argument, NULL, 'p'},
        {"stop-after", required_argument, NULL, 's'},
        {"startLim", required_argument, NULL, 'l'},
        {NULL, 0, NULL, 0}
    };

    char ch;
    while ((ch = getopt_long(argc, argv, "", longopts, NULL)) != -1) {
        switch (ch) {
        case 'i':
            in_filename = optarg;
            break;
        case 'p':
            print_solutions = true;
            break;
        case 's':
            max_solutions = atoll(optarg);
            break;
        case 'v':
            report_delta = atoll(optarg);
            break;
        case 'l':
            NUMBER_OF_OPTIONS_TO_START = atoi(optarg);
            break;
        default:
            errx(1, "Unknown option\n");
        }
    }
    if (in_filename == NULL) {
        usage(argv);
        return 0; //Fail
    }
    next_report = report_delta;
    return 1; //Success
}

int findNameLength(char *namePointer) {
	int length = 0;
	while (namePointer[++length] != '\0' && length < 64);
	return length < 64 ? length : 1;
}

void sendInstance(instance_t *instance, int targetRank) {
	int values[3] = {instance->n_items, instance->n_primary, instance->n_options};
	MPI_Send(values, 3, MPI_INT, targetRank, 0, MPI_COMM_WORLD);
	for (int i = 0 ; i < instance->n_items; ++i) {
		int nameLength = findNameLength(instance->item_name[i]);
		if (nameLength != 1) {
			MPI_Send(&nameLength, 1, MPI_INT, targetRank, 0, MPI_COMM_WORLD);
			MPI_Send(instance->item_name[i], nameLength, MPI_BYTE, targetRank, 0, MPI_COMM_WORLD);
		}
		else {
			char tmp = '\0';
			MPI_Send(&nameLength, 1, MPI_INT, targetRank, 0, MPI_COMM_WORLD);
			MPI_Send(&tmp, 1, MPI_BYTE, targetRank, 0, MPI_COMM_WORLD);			
		}
	}
	MPI_Send(instance->options, instance->n_options * instance->n_items, MPI_INT, targetRank, 0, MPI_COMM_WORLD);
	MPI_Send(instance->ptr, instance->n_options + 1, MPI_INT, targetRank, 0, MPI_COMM_WORLD);
}

instance_t *receiveInstance() {
	instance_t *newInstance = malloc(sizeof(instance_t));
	int values[3];
	MPI_Recv(values, 3, MPI_INT, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, NULL);
	newInstance->n_items = values[0];
	newInstance->n_primary = values[1];
	newInstance->n_options = values[2];

	newInstance->item_name = malloc(newInstance->n_items * sizeof(char *));
	newInstance->ptr = malloc((newInstance->n_options + 1) * sizeof(char *));
	newInstance->options = malloc(newInstance->n_items * newInstance->n_options * sizeof(char *));

	for (int i = 0 ; i < newInstance->n_items ; ++i) {
		int nameLength;
		MPI_Recv(&nameLength, 1, MPI_INT, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, NULL);
		newInstance->item_name[i] = malloc(nameLength);
		MPI_Recv(newInstance->item_name[i], nameLength, MPI_BYTE, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, NULL);
	}
	MPI_Recv(newInstance->options, newInstance->n_options * newInstance->n_items, MPI_INT, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, NULL);
	MPI_Recv(newInstance->ptr, newInstance->n_options + 1, MPI_INT, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, NULL);
	return newInstance;
}

void sendSparseArray(sparse_array_t *sparseArray, int targetRank) {
	int values[2] = {sparseArray->len, sparseArray->capacity};
	int *arrays = malloc(2 * sparseArray->capacity * sizeof(int));
	memcpy(arrays, sparseArray->p, sparseArray->capacity * sizeof(int));
	memcpy(arrays + sparseArray->capacity, sparseArray->q, sparseArray->capacity * sizeof(int));

	MPI_Send(values, 2, MPI_INT, targetRank, 0, MPI_COMM_WORLD);
	MPI_Send(arrays, 2 * sparseArray->capacity, MPI_INT, targetRank, 0, MPI_COMM_WORLD);
	free(arrays);
}

sparse_array_t *receiveSparseArray() {
	sparse_array_t *newSparseArray = malloc(sizeof(sparse_array_t));
	int values[2];
	MPI_Recv(values, 2, MPI_INT, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, NULL);
	newSparseArray->len = values[0];
	newSparseArray->capacity = values[1];
	int *arrays = malloc(2 * newSparseArray->capacity * sizeof(int));
	MPI_Recv(arrays, 2 * newSparseArray->capacity, MPI_INT, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, NULL);

	newSparseArray->p = malloc(newSparseArray->capacity * sizeof(int));
	newSparseArray->q = malloc(newSparseArray->capacity * sizeof(int));
	memcpy(newSparseArray->p, arrays, newSparseArray->capacity * sizeof(int));
	memcpy(newSparseArray->q, arrays + newSparseArray->capacity, newSparseArray->capacity * sizeof(int));

	free(arrays);
	return newSparseArray;
}

void receiveOtherSparseArray(sparse_array_t *newSparseArray) {
    int values[2];
    MPI_Recv(values, 2, MPI_INT, 0, 0, MPI_COMM_WORLD, NULL);
    newSparseArray->len = values[0];
    newSparseArray->capacity = values[1];
    int *arrays = malloc(2 * newSparseArray->capacity * sizeof(int));
    MPI_Recv(arrays, 2 * newSparseArray->capacity, MPI_INT, 0, 0, MPI_COMM_WORLD, NULL);

    memcpy(newSparseArray->p, arrays, newSparseArray->capacity * sizeof(int));
    memcpy(newSparseArray->q, arrays + newSparseArray->capacity, newSparseArray->capacity * sizeof(int));

    free(arrays);
    return;
}

void copyOtherSparseArray(sparse_array_t *newSparseArray, sparse_array_t *oldSparseArray) {
    newSparseArray->len = oldSparseArray->len;
    newSparseArray->capacity = oldSparseArray->capacity;

    memcpy(newSparseArray->p, oldSparseArray->p, newSparseArray->capacity * sizeof(int));
    memcpy(newSparseArray->q, oldSparseArray->q, newSparseArray->capacity * sizeof(int));
}

void sendContext(context_t *contexte, int targetRank, int n) {
	int *arrays = malloc((3 * n + 1) * sizeof(int));
	memcpy(arrays,			contexte->chosen_options, n);
	memcpy(arrays + n,		contexte->child_num, n);
	memcpy(arrays + 2 * n,	contexte->num_children, n);
	arrays[3 * n] = contexte->level;
	MPI_Send(arrays, 3 * n + 1, MPI_INT, targetRank, 0, MPI_COMM_WORLD);

	sendSparseArray(contexte->active_items, targetRank);
	for (int i = 0; i < n ; ++i)
		sendSparseArray(contexte->active_options[i], targetRank);

	free(arrays);
}

context_t *receiveContext(int n) {
	context_t *newContext = malloc(sizeof(context_t));

	newContext->nodes = 0;
	newContext->solutions = 0;

	newContext->chosen_options = malloc(n * sizeof(int));
	newContext->child_num = 	 malloc(n * sizeof(int));
	newContext->num_children = 	 malloc(n * sizeof(int));
	newContext->active_options = malloc(n * sizeof(sparse_array_t));

	int *arrays = malloc((3 * n + 1) * sizeof(int));
	MPI_Recv(arrays, 3 * n + 1, MPI_INT, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, NULL);
	memcpy(newContext->chosen_options, 	arrays, n);
	memcpy(newContext->child_num, 		arrays + n, n);
	memcpy(newContext->num_children, 	arrays + 2 * n, n);
	newContext->level = arrays[3 * n];

	newContext->active_items = receiveSparseArray();
	for (int i = 0 ; i < n ; ++i)
		newContext->active_options[i] = receiveSparseArray();

	free(arrays);
	return newContext;
}

void sendOptions(context_t *contexte, int targetRank, int n) {
    int *temporary = malloc((n + 1) * sizeof(int));
    memcpy(temporary, contexte->chosen_options, n);
    temporary[n] = contexte->level;
    MPI_Send(temporary, n + 1, MPI_INT, targetRank, 0, MPI_COMM_WORLD);

    free(temporary);
}

void receiveOptions(instance_t *instance, context_t *newContext, int n) {
    int *temporary = malloc((n + 1) * sizeof(int));
    MPI_Recv(temporary, n + 1, MPI_INT, 0, 0, MPI_COMM_WORLD, NULL);
    memcpy(newContext->chosen_options, temporary, n);
    newContext->level = temporary[n];

    for (int i = 0 ; i < temporary[n] ; ++i) {
        int option = newContext->chosen_options[i];
        for (int p = instance->ptr[option]; p < instance->ptr[option + 1]; ++p) {
            int item = instance->options[p];
            cover(instance, newContext, item);
        }
    }
    free(temporary);
}

void receiveOtherContext(instance_t *instance, context_t *newContext, context_t *oldContext, int n) {
    newContext->nodes = 0;
    newContext->solutions = 0;
    newContext->level = 0;

    memcpy(newContext->chosen_options,  oldContext->chosen_options, n);
    memcpy(newContext->child_num,       oldContext->child_num, n);
    memcpy(newContext->num_children,    oldContext->num_children, n);

    copyOtherSparseArray(newContext->active_items, oldContext->active_items);
    for (int i = 0 ; i < n ; ++i)
        copyOtherSparseArray(newContext->active_options[i], oldContext->active_options[i]);

    receiveOptions(instance, newContext, n);

    return;
}
/*
void receiveOtherContext(context_t *newContext, int n) {
    newContext->nodes = 0;
    newContext->solutions = 0;

    int *arrays = malloc((3 * n + 1) * sizeof(int));
    MPI_Recv(arrays, 3 * n + 1, MPI_INT, 0, 0, MPI_COMM_WORLD, NULL);
    memcpy(newContext->chosen_options,  arrays, n);
    memcpy(newContext->child_num,       arrays + n, n);
    memcpy(newContext->num_children,    arrays + 2 * n, n);
    newContext->level = arrays[3 * n];

    newContext->active_items = receiveOtherSparseArray();
    for (int i = 0 ; i < n ; ++i)
        newContext->active_options[i] = receiveOtherSparseArray();

    free(arrays);
    return;
}
*/
instance_t *broadcastInstance(instance_t *instance, int myRank, int processusNumber) {
	if (myRank)
		instance = receiveInstance();
	int currentTarget = 1;
	while (currentTarget <= myRank)
		currentTarget *= 2;
	while (currentTarget + myRank < processusNumber) {
		sendInstance(instance, myRank + currentTarget);
		currentTarget *= 2;
	}
	return instance;
}

context_t *broadcastContext(context_t *context, int myRank, int processusNumber, int n) {
    if (myRank)
        context = receiveContext(n);
    int currentTarget = 1;
    while (currentTarget <= myRank)
        currentTarget *= 2;
    while (currentTarget + myRank < processusNumber) {
        sendContext(context, myRank + currentTarget, n);
        currentTarget *= 2;
    }
    return context;
}

#define willingToWork 1
#define gotResults 2
#define MAXLEVEL 2


void saveStatus(int *arrayIfDone, int maxValue) {
    FILE *myFile = fopen(saveFile, "w");
    fprintf(myFile, "%lf\n", wtime() - start + timeOffset);
    fprintf(myFile, "%d\n", maxValue);
    for (int i = 0 ; i < maxValue ; ++i)
        fprintf(myFile, "%d ", arrayIfDone[i]);
    fclose(myFile);

    if (rename(saveFile, saveFileNewName))
        printf("Unable to rename file\n");
}

char recoverStatus(int **arrayIfDone) {
    int temporary_numberOfElemens = NUMBER_OF_OPTIONS_TO_START;
    FILE *myFile = fopen(saveFileNewName, "r");
    if (myFile) {
        if (!fscanf(myFile, "%lf", &timeOffset))
            printf("Error while reading the time already spent\n");

        if (!fscanf(myFile, "%d", &NUMBER_OF_OPTIONS_TO_START)) {
            printf("Error while reading the number of elements\n");
            NUMBER_OF_OPTIONS_TO_START = temporary_numberOfElemens;
            return 0;
        }
        *arrayIfDone = malloc(NUMBER_OF_OPTIONS_TO_START * sizeof(int));
        for (int i = 0 ; i < NUMBER_OF_OPTIONS_TO_START ; ++i)
            if (!fscanf(myFile, "%d", &((*arrayIfDone)[i]))) {
                printf("Error while reading values\n");
                free(*arrayIfDone);
                return 0;
        }
        return 1;
    }
    return 0;
}

context_t **initMPISolve(instance_t *instance, context_t *ctx, int *queueLength) {
    context_t **queue = malloc((2048 + NUMBER_OF_OPTIONS_TO_START) * sizeof(context_t*)); //Surralocation, making sure we can stock it
    int n_items = instance->n_items;
    queue[0] = copy_context(ctx, n_items);
    int currentNumber = 1;  //How many contexts we currently have
    int currentSize = 1;    //Before adding new ones, how many did we have

    while (currentNumber < NUMBER_OF_OPTIONS_TO_START) {
        for (int i = 0 ; i < currentSize ; ++i) {
            if (sparse_array_empty(queue[i]->active_items)) {
                ++ctx->solutions;
                free_context(&queue[i], instance->n_items);
                if (currentNumber == 1)
                    break; //We found every single solution there was, while initiating :)
                queue[i--] = queue[--currentSize]; //get a new one
                queue[currentSize] = queue[--currentNumber]; //Not leave blanks
                break;
            }
            int chosen_item = choose_next_item(queue[i]);
            sparse_array_t *active_options = queue[i]->active_options[chosen_item];
            if (sparse_array_empty(active_options)) {
                ++ctx->solutions;
                free_context(&queue[i], n_items);
                queue[i--] = queue[--currentNumber];
                break;
            }
            cover(instance, queue[i], chosen_item);
            queue[i]->num_children[queue[i]->level] = active_options->len;

            for (int k = 0; k < active_options->len; k++) {
                int option = active_options->p[k];
                if (k + 1 == active_options->len) { //Last one
                    queue[i]->child_num[queue[i]->level] = k;
                    choose_option(instance, queue[i], option, chosen_item);
                    break;
                }
                queue[currentNumber] = copy_context(queue[i], n_items);
                queue[currentNumber]->child_num[queue[currentNumber]->level] = k;
                choose_option(instance, queue[currentNumber++], option, chosen_item);
            }
            if (currentNumber >= NUMBER_OF_OPTIONS_TO_START)
                break;
        }
        currentSize = currentNumber;
    }
    *queueLength = currentNumber;
    return queue;
}

void MPIMasterSolve(instance_t *instance, context_t *ctx, int processusNumber) {
    int numberOfElements;
    int solutionsFound = 0;
    int numberToDodge = 0;
    char endOfWork = 0;
    char workingStatus = 0;
    int n_items = instance->n_items;
    MPI_Status status;
    MPI_Request request;
    int *arrayIfDone;
    int *processDone;
    context_t **queue;
    
    if (!recoverStatus(&arrayIfDone)) {
        queue = initMPISolve(instance, ctx, &numberOfElements);
        arrayIfDone = malloc(numberOfElements * sizeof(int));
        for (int i = 0 ; i < numberOfElements ; ++i)
            arrayIfDone[i] = 0;
    }
    else {
        printf("Successfully recovered from a save\n");
        queue = initMPISolve(instance, ctx, &numberOfElements);
    }

    processDone = malloc(processusNumber * sizeof(int));        

    saveStatus(arrayIfDone, numberOfElements);
    double beginning = wtime();

    printf("%d\n", numberOfElements);
    {
        int messageReceived = 0;
        int i = 0;
        for (; i < numberOfElements ; ++i) {
            MPI_Irecv(&workingStatus, 1, MPI_CHAR, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &request);
            while (1) {
                MPI_Test(&request, &messageReceived, &status);
                if (messageReceived)
                    break;
                if (wtime() - beginning > 600) {
                    saveStatus(arrayIfDone, numberOfElements);
                    beginning = wtime();
                }
            }
            if (workingStatus == gotResults) {
                MPI_Recv(&solutionsFound, 1, MPI_INT, status.MPI_SOURCE, 0, MPI_COMM_WORLD, &status);
                arrayIfDone[processDone[status.MPI_SOURCE]] = 1;
                ctx->solutions += solutionsFound;
                if (ctx->solutions >= max_solutions) {
                    endOfWork = 1;
                    MPI_Send(&endOfWork, 1, MPI_CHAR, status.MPI_SOURCE, 0, MPI_COMM_WORLD);
                    numberToDodge = status.MPI_SOURCE;
                    break;
                }
            }
            MPI_Send(&endOfWork, 1, MPI_CHAR, status.MPI_SOURCE, 0, MPI_COMM_WORLD);
            sendOptions(queue[i], status.MPI_SOURCE, n_items);
            processDone[status.MPI_SOURCE] = i;
            free_context(&queue[i], n_items);
        }
        endOfWork = 1;
        for (; i < numberOfElements ; ++i)
            free_context(&queue[i], n_items);
        free(queue);
    }
    
    for (int i = 1 ; i < processusNumber ; ++i)
        if (i != numberToDodge) {
            MPI_Recv(&workingStatus, 1, MPI_CHAR, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &status);
            if (workingStatus == gotResults) {
                MPI_Recv(&solutionsFound, 1, MPI_INT, status.MPI_SOURCE, 0, MPI_COMM_WORLD, &status);
                ctx->solutions += solutionsFound;
            }
            MPI_Send(&endOfWork, 1, MPI_CHAR, status.MPI_SOURCE, 0, MPI_COMM_WORLD);
        }

    remove(saveFileNewName);
    return;
}

void MPISolve(instance_t *instance, context_t *ctx, int myRank, int processusNumber) {
    if (myRank) {
        char endOfWork = -1;
        char workingStatus = willingToWork;
        int solutionsFound = 0;

        int n_items = instance->n_items;

        context_t *newContext = copy_context(ctx, n_items);

        while (1) {
            MPI_Send(&workingStatus, 1, MPI_CHAR, 0, 0, MPI_COMM_WORLD);
            if (workingStatus == gotResults)
                MPI_Send(&solutionsFound, 1, MPI_INT, 0, 0, MPI_COMM_WORLD);

            MPI_Recv(&endOfWork, 1, MPI_CHAR, 0, 0, MPI_COMM_WORLD, NULL);
            if (endOfWork == 1) {
                free_context(&newContext, instance->n_items);
                free_context(&ctx, instance->n_items);
                return;
            }
            receiveOtherContext(instance, newContext, ctx, instance->n_items);
            solve(instance, newContext);
            workingStatus = gotResults;
            solutionsFound = newContext->solutions;
        }
    }
    MPIMasterSolve(instance, ctx, processusNumber);
    return;
}

int main(int argc, char **argv) {
	int myRank; /* rang du processus */
	int processusNumber; /* nombre de processus */

	/* Initialisation */
	MPI_Init(&argc, &argv);
	MPI_Comm_rank(MPI_COMM_WORLD, &myRank);
	MPI_Comm_size(MPI_COMM_WORLD, &processusNumber);
    NUMBER_OF_OPTIONS_TO_START = 200 * processusNumber;

	instance_t *instance = NULL;
	context_t *ctx = NULL;

	if (!myRank) {
		if (!initiation(argc, argv)){
			MPI_Finalize();
			return EXIT_FAILURE;
		}

	    instance = load_matrix(in_filename);
	    ctx = backtracking_setup(instance);

        {
            int lastSlashPos = 0;
            int currentSize = 0;
            while (1) {
                if (in_filename[currentSize] == '/')
                    lastSlashPos = currentSize + 1;
                if (in_filename[currentSize++] == '\0')
                    break;
            }
            currentSize = 0;

            while (currentSize < 100) {
                if (in_filename[lastSlashPos] != '\0')
                    saveFile[currentSize] = in_filename[lastSlashPos];
                else
                    break;
                ++currentSize;
                ++lastSlashPos;
            }
            if (currentSize >= 95) {
                printf("Filename too long, cannot save in case of failure, please shorten it (consider renaming the instance)\n");
                MPI_Finalize();
                exit(EXIT_FAILURE);
            }
            currentSize -= 3; //.ec
            saveFile[currentSize++] = '_';
            saveFile[currentSize++] = 'M';
            saveFile[currentSize++] = 'P';
            saveFile[currentSize++] = 'I';
            saveFile[currentSize++] = '.';
            saveFile[currentSize++] = 'b';
            saveFile[currentSize++] = 'a';
            saveFile[currentSize++] = 'k';
            saveFile[currentSize]   = '\0';

            memcpy(saveFileNewName, saveFile, currentSize);
            currentSize -= 3;
            saveFileNewName[currentSize++] = 'r';
            saveFileNewName[currentSize++] = 'e';
            saveFileNewName[currentSize++] = 's';
            saveFileNewName[currentSize]   = '\0';
            //Preparing our files
        }
	}


	instance = broadcastInstance(instance, myRank, processusNumber); //Now everyone knows the instance we work on
    ctx = broadcastContext(ctx, myRank, processusNumber, instance->n_items); //Mallocs accordingly

	if (!myRank)
	    start = wtime();

    FILE *myFile = NULL;

    if (!myRank) {

        char measureFile[101];
        {
            int lastSlashPos = 0;
            int currentSize = 0;
            while (1) {
                if (in_filename[currentSize] == '/')
                    lastSlashPos = currentSize + 1;
                if (in_filename[currentSize++] == '\0')
                    break;
            }
            currentSize = 0;

            while (currentSize < 100) {
                if (in_filename[lastSlashPos] != '\0')
                    measureFile[currentSize] = in_filename[lastSlashPos];
                else
                    break;
                ++currentSize;
                ++lastSlashPos;
            }
            if (currentSize >= 95) {
                printf("Filename too long, cannot save in case of failure, please shorten it (consider renaming the instance)\n");
                MPI_Finalize();
                exit(EXIT_FAILURE);
            }
            currentSize -= 3; //.ec
            measureFile[currentSize++] = '_';
            measureFile[currentSize++] = 'M';
            measureFile[currentSize++] = 'P';
            measureFile[currentSize++] = 'I';
            measureFile[currentSize++] = '.';
            measureFile[currentSize++] = 'm';
            measureFile[currentSize++] = 'e';
            measureFile[currentSize++] = 's';
            measureFile[currentSize]   = '\0';
        }

        myFile = fopen(measureFile, "w");
    }


    for (int i = 2 ; i <= processusNumber ; ++i) {
        if (i <= myRank)
            continue;
        if (!myRank)
            ctx = backtracking_setup(instance);
        ctx = broadcastContext(ctx, myRank, i, instance->n_items);
        start = wtime();

        #pragma omp parallel
        #pragma omp single
    	MPISolve(instance, ctx, myRank, i);
        #pragma omp taskwait
        if (myFile)
            fprintf(myFile, "%d %lf\n", i, wtime() - start);
    }


	if (!myRank) {
	    printf("FINI. Trouvé %lld solutions en %.1fs\n", ctx->solutions, wtime() - start + timeOffset);


	    free_context(&ctx, instance->n_items);
	    free_instance(instance);
	}
    MPI_Finalize();
    exit(EXIT_SUCCESS);
}



