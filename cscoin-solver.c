#include "cscoin-solver.h"
#include "cscoin-mt64.h"

#include <omp.h>
#include <openssl/sha.h>
#include <stdlib.h>
#include <string.h>

static gint
guint64cmp_asc (const void *a, const void *b)
{
    return *(guint64*) a < *(guint64*) b ? -1 : 1;
}

static gint
guint64cmp_desc (const void *a, const void *b)
{
    return *(guint64*) a > *(guint64*) b ? -1 : 1;
}

static void
solve_sorted_list_challenge (CSCoinMT64 *mt64,
                             SHA256_CTX *checksum,
                             gint        nb_elements)
{
    gint i;
    guint64 numbers[nb_elements];

    for (i = 0; i < nb_elements; i++)
    {
        numbers[i] = cscoin_mt64_next_uint64 (mt64);
    }

    qsort (numbers, nb_elements, sizeof (guint64), guint64cmp_asc);

    gchar number_str[32];
    for (i = 0; i < nb_elements; i++)
    {
        g_snprintf (number_str, 32, "%lu", numbers[i]);
        SHA256_Update (checksum, number_str, strlen (number_str));
    }
}

static void
solve_reverse_sorted_list_challenge (CSCoinMT64 *mt64,
                                     SHA256_CTX *checksum,
                                     gint        nb_elements)
{
    gint i;
    guint64 numbers[nb_elements];

    for (i = 0; i < nb_elements; i++)
    {
        numbers[i] = cscoin_mt64_next_uint64 (mt64);
    }

    qsort (numbers, nb_elements, sizeof (guint64), guint64cmp_desc);

    gchar number_str[32];
    for (i = 0; i < nb_elements; i++)
    {
        g_snprintf (number_str, 32, "%lu", numbers[i]);
        SHA256_Update (checksum, number_str, strlen (number_str));
    }
}

typedef enum _CSCoinShortestPathTileType CSCoinShortestPathTileType;

enum _CSCoinShortestPathTileType
{
    CSCOIN_SHORTEST_PATH_TILE_TYPE_BLANK    = 0x0,
    CSCOIN_SHORTEST_PATH_TILE_TYPE_ENTRY    = 0x1,
    CSCOIN_SHORTEST_PATH_TILE_TYPE_EXIT     = 0x2,
    CSCOIN_SHORTEST_PATH_TILE_TYPE_FRONTIER = 0x3
};

static guint8
cscoin_shortest_path_build_tile (CSCoinShortestPathTileType a,
                                 CSCoinShortestPathTileType b,
                                 CSCoinShortestPathTileType c,
                                 CSCoinShortestPathTileType d)
{
    return a | b << 2 | c << 4 | d << 6;
}

typedef enum _CSCoinShortestPathDirection CSCoinShortestPathDirection;

enum _CSCoinShortestPathDirection
{
    CSCOIN_SHORTEST_PATH_DIRECTION_UP_LEFT    = 0,
    CSCOIN_SHORTEST_PATH_DIRECTION_UP_RIGHT   = 1,
    CSCOIN_SHORTEST_PATH_DIRECTION_RIGHT_UP   = 2,
    CSCOIN_SHORTEST_PATH_DIRECTION_RIGHT_DOWN = 3,
    CSCOIN_SHORTEST_PATH_DIRECTION_DOWN_RIGHT = 4,
    CSCOIN_SHORTEST_PATH_DIRECTION_DOWN_LEFT  = 5,
    CSCOIN_SHORTEST_PATH_DIRECTION_LEFT_DOWN  = 6,
    CSCOIN_SHORTEST_PATH_DIRECTION_LEFT_UP    = 7
};

/**
 * Indicate the cost reaching exit on the tile when coming from a direction.
 *
 * The tiles are indexed such that each 2-bit pack represent a tile
 *
 *  - 00 is a blank
 *  - 01 is an entry
 *  - 10 is an exit
 *  - 11 is a frontier
 *
 * The direction are clockwise:
 *
 *  - up-left
 *  - up-right
 *  - right-up
 *  - right-down
 *  - down-right
 *  - down-left
 *  - left-down
 *  - left-up
 *
 * If there is no exit, then the cost is '0', which indicates that the
 * tile should be skipped.
 */
static guint8
CSCOIN_SHORTEST_PATH_TILE_COST_PER_DIRECTION[256][8] =
{
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_00_00_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_00_00_01
    {2, 3, 3, 2, 2, 1, 1, 2}, // 0b00_00_00_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_00_00_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_00_01_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_00_01_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_00_01_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_00_01_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_01_00_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_01_00_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_01_00_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_01_00_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_01_01_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_01_01_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_01_01_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_01_01_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_10_00_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_10_00_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_10_00_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_10_00_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_10_01_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_10_01_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_10_01_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_11_01_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_11_00_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_11_00_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_11_00_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_11_00_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_11_01_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_11_01_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_11_01_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_11_01_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_00_00_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_00_00_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_00_00_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_00_00_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_00_01_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_00_01_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_00_01_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_00_01_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_01_00_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_01_00_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_01_00_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_01_00_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_01_01_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_01_01_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_01_01_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_01_01_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_10_00_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_10_00_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_10_00_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_10_00_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_10_01_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_10_01_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_10_01_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_10_01_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_11_00_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_11_00_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_11_00_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_11_00_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_11_01_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_11_01_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_11_01_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_11_01_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_00_00_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_00_00_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_00_00_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_00_00_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_00_01_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_00_01_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_00_01_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_00_01_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_01_00_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_01_00_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_01_00_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_01_00_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_01_01_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_01_01_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_01_01_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_01_01_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_10_00_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_10_00_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_10_00_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_10_00_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_10_01_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_10_01_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_10_01_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_10_01_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_11_00_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_11_00_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_11_00_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_11_00_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_11_01_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_11_01_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_11_01_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b00_11_01_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_00_00_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_00_00_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_00_00_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_00_00_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_00_01_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_00_01_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_00_01_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_00_01_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_01_00_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_01_00_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_01_00_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_01_00_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_01_01_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_01_01_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_01_01_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_01_01_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_10_00_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_10_00_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_10_00_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_10_00_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_10_01_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_10_01_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_10_01_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_10_01_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_11_00_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_11_00_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_11_00_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_11_00_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_11_01_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_11_01_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_11_01_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b01_11_01_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_00_00_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_00_00_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_00_00_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_00_00_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_00_01_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_00_01_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_00_01_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_00_01_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_01_00_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_01_00_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_01_00_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_01_00_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_01_01_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_01_01_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_01_01_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_01_01_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_10_00_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_10_00_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_10_00_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_10_00_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_10_01_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_10_01_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_10_01_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_10_01_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_11_00_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_11_00_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_11_00_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_11_00_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_11_01_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_11_01_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_11_01_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_11_01_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_00_00_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_00_00_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_00_00_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_00_00_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_00_01_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_00_01_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_00_01_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_00_01_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_01_00_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_01_00_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_01_00_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_01_00_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_01_01_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_01_01_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_01_01_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_01_01_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_10_00_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_10_00_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_10_00_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_10_00_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_10_01_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_10_01_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_10_01_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_10_01_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_11_00_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_11_00_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_11_00_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_11_00_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_11_01_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_11_01_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_11_01_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_11_01_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_00_00_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_00_00_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_00_00_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_00_00_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_00_01_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_00_01_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_00_01_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_00_01_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_01_00_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_01_00_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_01_00_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_01_00_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_01_01_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_01_01_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_01_01_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_01_01_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_10_00_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_10_00_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_10_00_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_10_00_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_10_01_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_10_01_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_10_01_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_10_01_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_11_00_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_11_00_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_11_00_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_11_00_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_11_01_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_11_01_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_11_01_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b10_11_01_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_00_00_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_00_00_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_00_00_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_00_00_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_00_01_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_00_01_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_00_01_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_00_01_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_01_00_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_01_00_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_01_00_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_01_00_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_01_01_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_01_01_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_01_01_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_01_01_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_10_00_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_10_00_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_10_00_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_10_00_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_10_01_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_10_01_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_10_01_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_10_01_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_11_00_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_11_00_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_11_00_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_11_00_11
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_11_01_00
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_11_01_01
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_11_01_10
    {0, 0, 0, 0, 0, 0, 0, 0}, // 0b11_11_01_11
};

static void
solve_shortest_path_challenge (CSCoinMT64 *mt64,
                               SHA256_CTX *checksum,
                               gint        grid_size,
                               gint        nb_blockers)
{
    // TODO
    guint8 cost;

    /* cost for reaching exit located at (1, 1) in a 4x4 tile coming from up-left */
    cost = CSCOIN_SHORTEST_PATH_TILE_COST_PER_DIRECTION
        [cscoin_shortest_path_build_tile (CSCOIN_SHORTEST_PATH_TILE_TYPE_BLANK,
                                          CSCOIN_SHORTEST_PATH_TILE_TYPE_BLANK,
                                          CSCOIN_SHORTEST_PATH_TILE_TYPE_BLANK,
                                          CSCOIN_SHORTEST_PATH_TILE_TYPE_EXIT)]
        [CSCOIN_SHORTEST_PATH_DIRECTION_UP_LEFT];
}

gchar *
cscoin_solve_challenge (gint                        challenge_id,
                        CSCoinChallengeType         challenge_type,
                        const gchar                *last_solution_hash,
                        const gchar                *hash_prefix,
                        CSCoinChallengeParameters  *parameters,
                        GCancellable               *cancellable,
                        GError                    **error)
{
    gboolean done = FALSE;
    gchar *ret = NULL;
    guint16 hash_prefix_num;

    hash_prefix_num = GUINT16_FROM_BE (strtol (hash_prefix, NULL, 16));

    #pragma omp parallel
    {
        SHA256_CTX checksum;
        CSCoinMT64 mt64;
        union {
            guint8  digest[SHA256_DIGEST_LENGTH];
            guint64 seed;
            guint16 prefix;
        } checksum_digest;
        guint nonce;
        gchar nonce_str[32];

        cscoin_mt64_init (&mt64);

        /* OpenMP partitionning */
        guint nonce_from, nonce_to;
        guint nonce_partition_size = G_MAXUINT / omp_get_num_threads ();
        nonce_from = omp_get_thread_num () * nonce_partition_size;
        nonce_to   = nonce_from + nonce_partition_size;

        for (nonce = nonce_from; nonce < nonce_to; nonce++)
        {
            if (G_UNLIKELY (done || g_cancellable_is_cancelled (cancellable)))
            {
                break;
            }

            g_snprintf (nonce_str, 32, "%u", nonce);

            SHA256_Init (&checksum);
            SHA256_Update (&checksum, last_solution_hash, 64);
            SHA256_Update (&checksum, nonce_str, strlen (nonce_str));
            SHA256_Final (checksum_digest.digest, &checksum);

            cscoin_mt64_set_seed (&mt64, GUINT64_FROM_LE (checksum_digest.seed));

            SHA256_Init (&checksum);

            switch (challenge_type)
            {
                case CSCOIN_CHALLENGE_TYPE_SORTED_LIST:
                    solve_sorted_list_challenge (&mt64,
                                                 &checksum,
                                                 parameters->sorted_list.nb_elements);
                    break;
                case CSCOIN_CHALLENGE_TYPE_REVERSE_SORTED_LIST:
                    solve_reverse_sorted_list_challenge (&mt64,
                                                         &checksum,
                                                         parameters->reverse_sorted_list.nb_elements);
                    break;
                case CSCOIN_CHALLENGE_TYPE_SHORTEST_PATH:
                    solve_shortest_path_challenge (&mt64,
                                                   &checksum,
                                                   parameters->shortest_path.grid_size,
                                                   parameters->shortest_path.nb_blockers);
                    break;
                default:
                    /* cannot break from OpenMP section */
                    g_critical ("Unknown challenge type %d, nothing will be performed.", challenge_type);
            }

            SHA256_Final (checksum_digest.digest, &checksum);

            if (hash_prefix_num == GUINT16_FROM_LE (checksum_digest.prefix))
            {
                done = TRUE;
                ret = g_strdup (nonce_str);
            }
        }
    }

    if (g_cancellable_set_error_if_cancelled (cancellable, error))
    {
        return NULL;
    }

    return ret;
}
