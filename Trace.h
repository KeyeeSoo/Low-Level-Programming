#ifndef TRACE_H
#define TRACE_H

/*
 * Pure-C mouse-tracing detector. No OpenGL, DDS, or GLFW dependencies.
 * Coordinates are in the same top-left-origin pixel system as the client.
 * The target path must be ordered; either tracing direction is accepted.
 */

#define TRACE_MAX_TARGET_POINTS 2048
#define TRACE_MAX_STROKE_POINTS 16384

#define TRACE_TOLERANCE_PIXELS 14.0f
#define TRACE_START_RADIUS_PIXELS 20.0f
#define TRACE_END_RADIUS_PIXELS 20.0f
#define TRACE_MIN_ACCURACY 0.85f
#define TRACE_MIN_COMPLETION 0.97f

typedef struct {
    float x;
    float y;
} TracePoint;

typedef enum {
    TRACE_CIRCLE,
    TRACE_RECTANGLE,
    TRACE_STAR,
    TRACE_FLOWER,
    TRACE_CUSTOM
} TraceShape;

typedef enum {
    TRACE_READY,
    TRACE_DRAWING,
    TRACE_SUCCESS,
    TRACE_FAILED
} TraceStatus;

typedef struct {
    TracePoint target[TRACE_MAX_TARGET_POINTS];
    float cumulative_length[TRACE_MAX_TARGET_POINTS];
    int target_count;
    float target_length;
    TraceShape shape;

    TracePoint stroke[TRACE_MAX_STROKE_POINTS];
    int stroke_count;
    int progress_index;
    int direction;       /* 0 = undecided, +1 = original, -1 = reversed */
    float progress_length;
    float mouse_distance;
    float valid_distance;
    float unmatched_distance; /* mouse travel since last accepted target point */
    TracePoint previous_cursor;
    TraceStatus status;
} TraceSession;

/* Generate a ready-to-trace, CLOSED path around (cx, cy). */
int trace_set_shape(TraceSession *session, TraceShape shape, float cx, float cy);

/*
 * Set any supplied ordered outline. Closed=1 joins the final point to the first.
 * Automatically resamples points every ~4 pixels for consistent scoring.
 * Returns 1 on success, 0 on invalid/oversized geometry.
 */
int trace_set_path(TraceSession *session, const TracePoint *points,
                   int count, int closed);

void trace_reset(TraceSession *session);
void trace_mouse_down(TraceSession *session, float x, float y);
void trace_mouse_move(TraceSession *session, float x, float y);
void trace_mouse_up(TraceSession *session, float x, float y);

/* Ratios in the interval 0..1. */
float trace_accuracy(const TraceSession *session);
float trace_completion(const TraceSession *session);
const char *trace_shape_name(TraceShape shape);
const char *trace_status_name(TraceStatus status);


#endif
