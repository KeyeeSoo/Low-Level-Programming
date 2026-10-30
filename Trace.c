#include "Trace.h"

#include <math.h>
#include <stddef.h>

#define TRACE_PI 3.14159265358979323846f
#define TRACE_TARGET_SPACING 4.0f
#define TRACE_MOUSE_SPACING 3.0f

static float distance_between(TracePoint a, TracePoint b)
{
    return hypotf(a.x - b.x, a.y - b.y);
}

static TracePoint interpolate(TracePoint a, TracePoint b, float fraction)
{
    TracePoint result = {
        a.x + (b.x - a.x) * fraction,
        a.y + (b.y - a.y) * fraction
    };
    return result;
}

void trace_reset(TraceSession *session)
{
    if (session == NULL) return;
    session->stroke_count = 0;
    session->progress_index = 0;
    session->progress_length = 0.0f;
    session->mouse_distance = 0.0f;
    session->valid_distance = 0.0f;
    session->previous_cursor = (TracePoint){0, 0};
    session->status = TRACE_READY;
}

static int append_target(TraceSession *session, TracePoint p)
{
    int n = session->target_count;
    if (n >= TRACE_MAX_TARGET_POINTS) return 0;

    if (n != 0) {
        float segment = distance_between(session->target[n - 1], p);
        if (segment < 0.0001f) return 1;
        session->target_length += segment;
    }

    session->target[n] = p;
    session->cumulative_length[n] = session->target_length;
    session->target_count++;
    return 1;
}

int trace_set_path(TraceSession *session, const TracePoint *points,
                   int count, int closed)
{
    if (session == NULL || points == NULL || count < 2) return 0;

    session->target_count = 0;
    session->target_length = 0.0f;
    session->shape = TRACE_CUSTOM;
    trace_reset(session);

    if (!append_target(session, points[0])) return 0;

    int segments = count - 1 + (closed ? 1 : 0);
    for (int i = 0; i < segments; i++) {
        TracePoint a = points[i % count];
        TracePoint b = points[(i + 1) % count];
        float length = distance_between(a, b);
        if (length < 0.0001f) continue;

        int steps = (int)ceilf(length / TRACE_TARGET_SPACING);
        for (int j = 1; j <= steps; j++) {
            if (!append_target(session, interpolate(a, b, (float)j / steps))) {
                session->target_count = 0;
                session->target_length = 0.0f;
                return 0;
            }
        }
    }
    if (session->target_count < 2 || session->target_length <= 0.0f) {
        session->target_count = 0;
        return 0;
    }
    return 1;
}

int trace_set_shape(TraceSession *session, TraceShape shape, float cx, float cy)
{
    TracePoint points[512];
    int count = 0;

    if (shape == TRACE_CIRCLE || shape == TRACE_FLOWER) {
        count = (shape == TRACE_CIRCLE) ? 240 : 400;
        for (int i = 0; i < count; i++) {
            float angle = -TRACE_PI / 2.0f + 2.0f * TRACE_PI * i / count;
            float radius = 135.0f;
            if (shape == TRACE_FLOWER) {
                /* 5 radial lobes, with a petal centred at the top. */
                radius = 102.0f + 34.0f * cosf(5.0f * (angle + TRACE_PI / 2.0f));
            }
            points[i] = (TracePoint){cx + radius * cosf(angle),
                                     cy + radius * sinf(angle)};
        }
    } else if (shape == TRACE_RECTANGLE) {
        count = 4;
        points[0] = (TracePoint){cx - 165.0f, cy - 120.0f};
        points[1] = (TracePoint){cx + 165.0f, cy - 120.0f};
        points[2] = (TracePoint){cx + 165.0f, cy + 120.0f};
        points[3] = (TracePoint){cx - 165.0f, cy + 120.0f};
    } else if (shape == TRACE_STAR) {
        count = 10;
        for (int i = 0; i < count; i++) {
            float angle = -TRACE_PI / 2.0f + (float)i * TRACE_PI / 5.0f;
            float radius = (i % 2 == 0) ? 145.0f : 63.0f;
            points[i] = (TracePoint){cx + radius * cosf(angle),
                                     cy + radius * sinf(angle)};
        }
    } else {
        return 0;
    }

    if (!trace_set_path(session, points, count, 1)) return 0;
    session->shape = shape;
    return 1;
}

void trace_mouse_down(TraceSession *session, float x, float y)
{
    if (session == NULL || session->target_count < 2) return;
    TracePoint p = {x, y};

    /* Clicking elsewhere must not start an attempt. */
    if (distance_between(p, session->target[0]) > TRACE_START_RADIUS_PIXELS)
        return;

    trace_reset(session);
    session->status = TRACE_DRAWING;
    session->previous_cursor = p;
    session->stroke[session->stroke_count++] = p;
}

static void trace_sample(TraceSession *session, TracePoint p, float step)
{
    if (session->stroke_count >= TRACE_MAX_STROKE_POINTS) {
        session->status = TRACE_FAILED;
        return;
    }
    session->stroke[session->stroke_count++] = p;
    session->mouse_distance += step;

    /* Search only the next short section of the ordered path. This prevents
       jumping to a different petal/edge at intersections or near-contacts. */
    int start = session->progress_index - 3;
    if (start < 0) start = 0;
    int end = session->progress_index + 16;
    if (end >= session->target_count) end = session->target_count - 1;

    float best_distance = 1e30f;
    int best_index = -1;
    for (int i = start; i <= end; i++) {
        float d = distance_between(p, session->target[i]);
        /* Progress must be plausible for the distance the mouse moved. */
        float max_length = session->progress_length + 2.4f * step + 10.0f;
        if (d < best_distance &&
            session->cumulative_length[i] <= max_length) {
            best_distance = d;
            best_index = i;
        }
    }

    if (best_index >= 0 && best_distance <= TRACE_TOLERANCE_PIXELS) {
        session->valid_distance += step;
        if (best_index > session->progress_index) {
            session->progress_index = best_index;
            session->progress_length = session->cumulative_length[best_index];
        }
    }
}

void trace_mouse_move(TraceSession *session, float x, float y)
{
    if (session == NULL || session->status != TRACE_DRAWING) return;

    TracePoint from = session->previous_cursor;
    TracePoint to = {x, y};
    float length = distance_between(from, to);
    if (length < 0.0001f) return;

    /* Interpolate cursor events to avoid skipping path sections on fast moves. */
    int steps = (int)ceilf(length / TRACE_MOUSE_SPACING);
    TracePoint previous = from;
    for (int i = 1; i <= steps; i++) {
        TracePoint current = interpolate(from, to, (float)i / steps);
        trace_sample(session, current, distance_between(previous, current));
        previous = current;
        if (session->status != TRACE_DRAWING) break;
    }
    session->previous_cursor = to;
}

float trace_accuracy(const TraceSession *session)
{
    if (session == NULL || session->mouse_distance < 0.0001f) return 0.0f;
    return session->valid_distance / session->mouse_distance;
}

float trace_completion(const TraceSession *session)
{
    if (session == NULL || session->target_length < 0.0001f) return 0.0f;
    return session->progress_length / session->target_length;
}

void trace_mouse_up(TraceSession *session, float x, float y)
{
    if (session == NULL || session->status != TRACE_DRAWING) return;
    trace_mouse_move(session, x, y);
    if (session->status != TRACE_DRAWING) return;

    TracePoint release = {x, y};
    int finished_near_end =
        distance_between(release, session->target[session->target_count - 1])
        <= TRACE_END_RADIUS_PIXELS;

    if (trace_completion(session) >= TRACE_MIN_COMPLETION &&
        trace_accuracy(session) >= TRACE_MIN_ACCURACY && finished_near_end)
        session->status = TRACE_SUCCESS;
    else
        session->status = TRACE_FAILED;
}

const char *trace_shape_name(TraceShape shape)
{
    switch (shape) {
        case TRACE_CIRCLE: return "Circle";
        case TRACE_RECTANGLE: return "Rectangle";
        case TRACE_STAR: return "Star";
        case TRACE_FLOWER: return "Flower";
        default: return "Custom";
    }
}

const char *trace_status_name(TraceStatus status)
{
    switch (status) {
        case TRACE_READY: return "Click green dot";
        case TRACE_DRAWING: return "Tracing";
        case TRACE_SUCCESS: return "Success";
        case TRACE_FAILED: return "Try again (R)";
        default: return "Unknown";
    }
}
