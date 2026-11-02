#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "rikky_module_internal.h"

#define LSD_NOTDEF (-1024.0)
#define LSD_PI 3.14159265358979323846
#define LSD_RELATIVE_ERROR_FACTOR 100.0
#define LSD_DBL_EPSILON 2.2204460492503131e-16
#define LSD_DBL_MIN 2.2250738585072014e-308
#define LSD_DBL_MAX 1.7976931348623157e+308
#define LSD_TABSIZE 100000

typedef struct
{
    int x;
    int y;
} LsdPoint;

typedef struct LsdCoorList
{
    int x;
    int y;
    struct LsdCoorList *next;
} LsdCoorList;

typedef struct
{
    double *data;
    unsigned int xsize;
    unsigned int ysize;
} LsdImageDouble;

typedef struct
{
    unsigned char *data;
    unsigned int xsize;
    unsigned int ysize;
} LsdImageChar;

typedef struct
{
    unsigned int size;
    unsigned int max_size;
    unsigned int dim;
    double *values;
} LsdTupleList;

typedef struct
{
    double x1;
    double y1;
    double x2;
    double y2;
    double width;
    double x;
    double y;
    double theta;
    double dx;
    double dy;
    double prec;
    double p;
} LsdRect;

typedef struct
{
    double vx[4];
    double vy[4];
    double ys;
    double ye;
    int x;
    int y;
} LsdRectIter;

static double g_lsd_inv[LSD_TABSIZE];

static int lsd_double_equal(double a, double b)
{
    double abs_diff;
    double aa;
    double bb;
    double abs_max;

    if (a == b)
    {
        return 1;
    }
    abs_diff = fabs(a - b);
    aa = fabs(a);
    bb = fabs(b);
    abs_max = (aa > bb) ? aa : bb;
    if (abs_max < LSD_DBL_MIN)
    {
        abs_max = LSD_DBL_MIN;
    }
    return (abs_diff / abs_max) <= (LSD_RELATIVE_ERROR_FACTOR * LSD_DBL_EPSILON);
}

static double lsd_dist(double x1, double y1, double x2, double y2)
{
    return sqrt((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1));
}

static double lsd_angle_diff(double a, double b)
{
    a -= b;
    while (a <= -LSD_PI)
    {
        a += LSD_PI * 2.0;
    }
    while (a > LSD_PI)
    {
        a -= LSD_PI * 2.0;
    }
    if (a < 0.0)
    {
        a = -a;
    }
    return a;
}

static double lsd_angle_diff_signed(double a, double b)
{
    a -= b;
    while (a <= -LSD_PI)
    {
        a += LSD_PI * 2.0;
    }
    while (a > LSD_PI)
    {
        a -= LSD_PI * 2.0;
    }
    return a;
}

static int lsd_isaligned(int x, int y, const LsdImageDouble *angles, double theta, double prec)
{
    double a;

    if (!angles || !angles->data)
    {
        return 0;
    }
    a = angles->data[(size_t)y * angles->xsize + (size_t)x];
    if (a == LSD_NOTDEF)
    {
        return 0;
    }
    theta -= a;
    if (theta < 0.0)
    {
        theta = -theta;
    }
    if (theta > LSD_PI * 1.5)
    {
        theta -= LSD_PI * 2.0;
        if (theta < 0.0)
        {
            theta = -theta;
        }
    }
    return theta <= prec;
}

static void lsd_free_image_double(LsdImageDouble *i)
{
    if (i)
    {
        rm_free(i->data);
        rm_free(i);
    }
}

static LsdImageDouble *lsd_new_image_double(unsigned int xsize, unsigned int ysize)
{
    LsdImageDouble *image;

    if (xsize == 0 || ysize == 0)
    {
        return NULL;
    }
    image = (LsdImageDouble *)rm_malloc(sizeof(LsdImageDouble));
    if (!image)
    {
        return NULL;
    }
    image->data = (double *)rm_calloc((size_t)xsize * (size_t)ysize, sizeof(double));
    if (!image->data)
    {
        rm_free(image);
        return NULL;
    }
    image->xsize = xsize;
    image->ysize = ysize;
    return image;
}

static LsdImageChar *lsd_new_image_char_ini(unsigned int xsize, unsigned int ysize, unsigned char fill)
{
    LsdImageChar *image;
    size_t count;

    if (xsize == 0 || ysize == 0)
    {
        return NULL;
    }
    image = (LsdImageChar *)rm_malloc(sizeof(LsdImageChar));
    if (!image)
    {
        return NULL;
    }
    count = (size_t)xsize * (size_t)ysize;
    image->data = (unsigned char *)rm_calloc(count, 1);
    if (!image->data)
    {
        rm_free(image);
        return NULL;
    }
    image->xsize = xsize;
    image->ysize = ysize;
    if (fill != 0)
    {
        memset(image->data, fill, count);
    }
    return image;
}

static void lsd_free_image_char(LsdImageChar *i)
{
    if (i)
    {
        rm_free(i->data);
        rm_free(i);
    }
}

static int lsd_add_7tuple(LsdTupleList *out, double v1, double v2, double v3,
                          double v4, double v5, double v6, double v7)
{
    double *base;

    if (!out || out->dim != 7)
    {
        return 0;
    }
    if (out->size == out->max_size)
    {
        double *grown;
        unsigned int new_max = out->max_size * 2u;
        grown = (double *)rm_realloc(out->values, (size_t)new_max * 7u * sizeof(double));
        if (!grown)
        {
            return 0;
        }
        out->values = grown;
        out->max_size = new_max;
    }
    base = out->values + (size_t)out->size * 7u;
    base[0] = v1;
    base[1] = v2;
    base[2] = v3;
    base[3] = v4;
    base[4] = v5;
    base[5] = v6;
    base[6] = v7;
    out->size++;
    return 1;
}

static void lsd_gaussian_kernel(LsdTupleList *kernel, double sigma, double mean)
{
    double sum = 0.0;
    unsigned int i;

    kernel->size = 1;
    for (i = 0; i < kernel->dim; i++)
    {
        double val = ((double)i - mean) / sigma;
        kernel->values[i] = exp(-0.5 * val * val);
        sum += kernel->values[i];
    }
    if (sum >= 0.0)
    {
        for (i = 0; i < kernel->dim; i++)
        {
            kernel->values[i] /= sum;
        }
    }
}

static LsdImageDouble *lsd_gaussian_sampler(const LsdImageDouble *in, double scale, double sigma_scale)
{
    LsdImageDouble *aux;
    LsdImageDouble *out;
    LsdTupleList *kernel;
    unsigned int n_col;
    unsigned int m_row;
    unsigned int h;
    unsigned int n;
    unsigned int x;
    unsigned int y;
    unsigned int i;
    double sigma;
    int double_x_size;
    int double_y_size;

    n_col = (unsigned int)ceil((double)in->xsize * scale);
    m_row = (unsigned int)ceil((double)in->ysize * scale);
    aux = lsd_new_image_double(n_col, in->ysize);
    if (!aux)
    {
        return NULL;
    }
    out = lsd_new_image_double(n_col, m_row);
    if (!out)
    {
        lsd_free_image_double(aux);
        return NULL;
    }

    sigma = (scale < 1.0) ? (sigma_scale / scale) : sigma_scale;
    h = (scale < 1.0) ? (unsigned int)ceil(sigma * 3.7169221888498387) : 3u;
    n = 1u + 2u * h;

    kernel = (LsdTupleList *)rm_malloc(sizeof(LsdTupleList));
    if (!kernel)
    {
        lsd_free_image_double(aux);
        lsd_free_image_double(out);
        return NULL;
    }
    kernel->size = 0;
    kernel->max_size = 1;
    kernel->dim = n;
    kernel->values = (double *)rm_malloc((size_t)n * sizeof(double));
    if (!kernel->values)
    {
        rm_free(kernel);
        lsd_free_image_double(aux);
        lsd_free_image_double(out);
        return NULL;
    }

    double_x_size = (int)in->xsize * 2;
    double_y_size = (int)in->ysize * 2;

    for (x = 0; x < aux->xsize; x++)
    {
        int xc = (int)floor((double)x / scale + 0.5);
        lsd_gaussian_kernel(kernel, sigma, (double)h + (double)x / scale - (double)xc);
        for (y = 0; y < aux->ysize; y++)
        {
            double sum = 0.0;
            for (i = 0; i < kernel->dim; i++)
            {
                int j = xc - (int)h + (int)i;
                while (j < 0)
                {
                    j += double_x_size;
                }
                while (j >= double_x_size)
                {
                    j -= double_x_size;
                }
                if (j >= (int)in->xsize)
                {
                    j = double_x_size - 1 - j;
                }
                sum += in->data[(size_t)y * in->xsize + (size_t)j] * kernel->values[i];
            }
            aux->data[(size_t)y * aux->xsize + (size_t)x] = sum;
        }
    }

    for (y = 0; y < out->ysize; y++)
    {
        int yc = (int)floor((double)y / scale + 0.5);
        lsd_gaussian_kernel(kernel, sigma, (double)h + (double)y / scale - (double)yc);
        for (x = 0; x < out->xsize; x++)
        {
            double sum = 0.0;
            for (i = 0; i < kernel->dim; i++)
            {
                int j = yc - (int)h + (int)i;
                while (j < 0)
                {
                    j += double_y_size;
                }
                while (j >= double_y_size)
                {
                    j -= double_y_size;
                }
                if (j >= (int)in->ysize)
                {
                    j = double_y_size - 1 - j;
                }
                sum += aux->data[(size_t)j * aux->xsize + (size_t)x] * kernel->values[i];
            }
            out->data[(size_t)y * out->xsize + (size_t)x] = sum;
        }
    }

    rm_free(kernel->values);
    rm_free(kernel);
    lsd_free_image_double(aux);
    return out;
}

static LsdImageDouble *lsd_ll_angle(const LsdImageDouble *in, double threshold,
                                    LsdCoorList **list_p, void **mem_p,
                                    LsdImageDouble **modgrad, unsigned int n_bins)
{
    LsdImageDouble *g;
    unsigned int n;
    unsigned int p;
    unsigned int x;
    unsigned int y;
    unsigned int i;
    double max_grad = 0.0;
    LsdCoorList *list;
    LsdCoorList **range_l_s;
    LsdCoorList **range_l_e;
    LsdCoorList *start;
    LsdCoorList *end;

    p = in->xsize;
    n = in->ysize;

    g = lsd_new_image_double(p, n);
    if (!g)
    {
        return NULL;
    }
    *modgrad = lsd_new_image_double(p, n);
    if (!*modgrad)
    {
        lsd_free_image_double(g);
        return NULL;
    }

    list = (LsdCoorList *)rm_calloc((size_t)n * (size_t)p, sizeof(LsdCoorList));
    range_l_s = (LsdCoorList **)rm_calloc(n_bins, sizeof(LsdCoorList *));
    range_l_e = (LsdCoorList **)rm_calloc(n_bins, sizeof(LsdCoorList *));
    if (!list || !range_l_s || !range_l_e)
    {
        rm_free(list);
        rm_free(range_l_s);
        rm_free(range_l_e);
        lsd_free_image_double(*modgrad);
        *modgrad = NULL;
        lsd_free_image_double(g);
        return NULL;
    }
    *mem_p = list;

    for (x = 0; x < p; x++)
    {
        g->data[(size_t)(n - 1u) * p + x] = LSD_NOTDEF;
    }
    for (y = 0; y < n; y++)
    {
        g->data[(size_t)y * p + (p - 1u)] = LSD_NOTDEF;
    }

    for (x = 0; x < p - 1u; x++)
    {
        for (y = 0; y < n - 1u; y++)
        {
            double com1 = in->data[(size_t)(y + 1u) * p + x + 1u] - in->data[(size_t)y * p + x];
            double com2 = in->data[(size_t)y * p + x + 1u] - in->data[(size_t)(y + 1u) * p + x];
            double gx = com1 + com2;
            double gy = com1 - com2;
            double norm2 = (gx * gx + gy * gy) * 0.25;
            double norm = sqrt(norm2);

            (*modgrad)->data[(size_t)y * p + x] = norm;
            if (norm <= threshold)
            {
                g->data[(size_t)y * p + x] = LSD_NOTDEF;
            }
            else
            {
                g->data[(size_t)y * p + x] = atan2(gx, -gy);
                if (norm > max_grad)
                {
                    max_grad = norm;
                }
            }
        }
    }

    for (x = 0; x < p - 1u; x++)
    {
        for (y = 0; y < n - 1u; y++)
        {
            LsdCoorList *pixel_node = list + (size_t)x * (size_t)(n - 1u) + (size_t)y;
            double norm = (*modgrad)->data[(size_t)y * p + x];

            i = (max_grad > 0.0) ? (unsigned int)(norm * (double)n_bins / max_grad) : 0u;
            if (i >= n_bins)
            {
                i = n_bins - 1u;
            }
            if (range_l_e[i] == NULL)
            {
                range_l_s[i] = pixel_node;
                range_l_e[i] = pixel_node;
            }
            else
            {
                range_l_e[i]->next = pixel_node;
                range_l_e[i] = pixel_node;
            }
            pixel_node->x = (int)x;
            pixel_node->y = (int)y;
            pixel_node->next = NULL;
        }
    }

    start = NULL;
    end = NULL;
    for (i = n_bins - 1u; i > 0; i--)
    {
        if (range_l_s[i] != NULL)
        {
            if (start == NULL)
            {
                start = range_l_s[i];
                end = range_l_e[i];
            }
            else
            {
                end->next = range_l_s[i];
                end = range_l_e[i];
            }
        }
    }
    if (range_l_s[0] != NULL)
    {
        if (start == NULL)
        {
            start = range_l_s[0];
        }
        else
        {
            end->next = range_l_s[0];
        }
    }
    *list_p = start;

    rm_free(range_l_s);
    rm_free(range_l_e);
    return g;
}

static void lsd_region_grow(int x, int y, const LsdImageDouble *angles, LsdPoint *reg,
                            int *reg_size, double *reg_angle, LsdImageChar *used, double prec)
{
    double sumdx;
    double sumdy;
    int i;

    *reg_size = 1;
    reg[0].x = x;
    reg[0].y = y;
    *reg_angle = angles->data[(size_t)y * angles->xsize + (size_t)x];
    sumdx = cos(*reg_angle);
    sumdy = sin(*reg_angle);
    used->data[(size_t)y * used->xsize + (size_t)x] = 1;

    for (i = 0; i < *reg_size; i++)
    {
        int xx;
        for (xx = reg[i].x - 1; xx <= reg[i].x + 1; xx++)
        {
            int yy;
            for (yy = reg[i].y - 1; yy <= reg[i].y + 1; yy++)
            {
                if (xx >= 0 && yy >= 0 && xx < (int)used->xsize && yy < (int)used->ysize &&
                    used->data[(size_t)yy * used->xsize + (size_t)xx] != 1 &&
                    lsd_isaligned(xx, yy, angles, *reg_angle, prec))
                {
                    double ang = angles->data[(size_t)yy * angles->xsize + (size_t)xx];
                    used->data[(size_t)yy * used->xsize + (size_t)xx] = 1;
                    reg[*reg_size].x = xx;
                    reg[*reg_size].y = yy;
                    (*reg_size)++;
                    sumdx += cos(ang);
                    sumdy += sin(ang);
                    *reg_angle = atan2(sumdy, sumdx);
                }
            }
        }
    }
}

static double lsd_get_theta(const LsdPoint *reg, int reg_size, double x, double y,
                            const LsdImageDouble *modgrad, double reg_angle, double prec)
{
    double ixx = 0.0;
    double iyy = 0.0;
    double ixy = 0.0;
    double lambda;
    double theta;
    int i;

    for (i = 0; i < reg_size; i++)
    {
        double weight = modgrad->data[(size_t)reg[i].y * modgrad->xsize + (size_t)reg[i].x];
        double dy = (double)reg[i].y - y;
        double dx = (double)reg[i].x - x;
        ixx += dy * dy * weight;
        iyy += dx * dx * weight;
        ixy -= dx * dy * weight;
    }

    lambda = 0.5 * (ixx + iyy - sqrt((ixx - iyy) * (ixx - iyy) + 4.0 * ixy * ixy));
    theta = (fabs(ixx) > fabs(iyy)) ? atan2(lambda - ixx, ixy) : atan2(ixy, lambda - iyy);
    if (lsd_angle_diff(theta, reg_angle) > prec)
    {
        theta += LSD_PI;
    }
    return theta;
}

static void lsd_region2rect(const LsdPoint *reg, int reg_size, const LsdImageDouble *modgrad,
                            double reg_angle, double prec, double p, LsdRect *rec)
{
    double x = 0.0;
    double y = 0.0;
    double sum = 0.0;
    double dx;
    double dy;
    double l_min = 0.0;
    double l_max = 0.0;
    double w_min = 0.0;
    double w_max = 0.0;
    double theta;
    int i;

    for (i = 0; i < reg_size; i++)
    {
        double weight = modgrad->data[(size_t)reg[i].y * modgrad->xsize + (size_t)reg[i].x];
        x += (double)reg[i].x * weight;
        y += (double)reg[i].y * weight;
        sum += weight;
    }
    x /= sum;
    y /= sum;

    theta = lsd_get_theta(reg, reg_size, x, y, modgrad, reg_angle, prec);
    dx = cos(theta);
    dy = sin(theta);

    for (i = 0; i < reg_size; i++)
    {
        double l = ((double)reg[i].x - x) * dx + ((double)reg[i].y - y) * dy;
        double w = -((double)reg[i].x - x) * dy + ((double)reg[i].y - y) * dx;
        if (l > l_max)
        {
            l_max = l;
        }
        if (l < l_min)
        {
            l_min = l;
        }
        if (w > w_max)
        {
            w_max = w;
        }
        if (w < w_min)
        {
            w_min = w;
        }
    }

    rec->x1 = x + l_min * dx;
    rec->y1 = y + l_min * dy;
    rec->x2 = x + l_max * dx;
    rec->y2 = y + l_max * dy;
    rec->width = w_max - w_min;
    rec->x = x;
    rec->y = y;
    rec->theta = theta;
    rec->dx = dx;
    rec->dy = dy;
    rec->prec = prec;
    rec->p = p;
    if (rec->width < 1.0)
    {
        rec->width = 1.0;
    }
}

static int lsd_reduce_region_radius(LsdPoint *reg, int *reg_size, const LsdImageDouble *modgrad,
                                    double reg_angle, double prec, double p, LsdRect *rec,
                                    LsdImageChar *used, double density_th)
{
    double density;
    double rad1;
    double rad2;
    double rad;
    double xc;
    double yc;

    density = (double)(*reg_size) / (lsd_dist(rec->x1, rec->y1, rec->x2, rec->y2) * rec->width);
    if (density >= density_th)
    {
        return 1;
    }

    xc = (double)reg[0].x;
    yc = (double)reg[0].y;
    rad1 = lsd_dist(xc, yc, rec->x1, rec->y1);
    rad2 = lsd_dist(xc, yc, rec->x2, rec->y2);
    rad = (rad1 > rad2) ? rad1 : rad2;

    while (density < density_th)
    {
        int i;
        rad *= 0.75;
        for (i = 0; i < *reg_size; i++)
        {
            if (lsd_dist(xc, yc, (double)reg[i].x, (double)reg[i].y) > rad)
            {
                used->data[(size_t)reg[i].y * used->xsize + (size_t)reg[i].x] = 0;
                reg[i].x = reg[*reg_size - 1].x;
                reg[i].y = reg[*reg_size - 1].y;
                (*reg_size)--;
                i--;
            }
        }
        if (*reg_size < 2)
        {
            return 0;
        }
        lsd_region2rect(reg, *reg_size, modgrad, reg_angle, prec, p, rec);
        density = (double)(*reg_size) / (lsd_dist(rec->x1, rec->y1, rec->x2, rec->y2) * rec->width);
    }
    return 1;
}

static int lsd_refine(LsdPoint *reg, int *reg_size, const LsdImageDouble *modgrad,
                      double reg_angle, double prec, double p, LsdRect *rec,
                      LsdImageChar *used, const LsdImageDouble *angles, double density_th)
{
    double density;
    int xc;
    int yc;
    double ang_c;
    double sum = 0.0;
    double s_sum = 0.0;
    double mean_angle;
    double tau;
    int n = 0;
    int i;

    density = (double)(*reg_size) / (lsd_dist(rec->x1, rec->y1, rec->x2, rec->y2) * rec->width);
    if (density >= density_th)
    {
        return 1;
    }

    xc = reg[0].x;
    yc = reg[0].y;
    ang_c = angles->data[(size_t)yc * angles->xsize + (size_t)xc];
    for (i = 0; i < *reg_size; i++)
    {
        used->data[(size_t)reg[i].y * used->xsize + (size_t)reg[i].x] = 0;
        if (lsd_dist((double)xc, (double)yc, (double)reg[i].x, (double)reg[i].y) < rec->width)
        {
            double angle = angles->data[(size_t)reg[i].y * angles->xsize + (size_t)reg[i].x];
            double ang_d = lsd_angle_diff_signed(angle, ang_c);
            sum += ang_d;
            s_sum += ang_d * ang_d;
            n++;
        }
    }
    mean_angle = sum / (double)n;
    tau = 2.0 * sqrt((s_sum - 2.0 * mean_angle * sum) / (double)n + mean_angle * mean_angle);

    lsd_region_grow(reg[0].x, reg[0].y, angles, reg, reg_size, &reg_angle, used, tau);
    if (*reg_size < 2)
    {
        return 0;
    }

    lsd_region2rect(reg, *reg_size, modgrad, reg_angle, prec, p, rec);
    density = (double)(*reg_size) / (lsd_dist(rec->x1, rec->y1, rec->x2, rec->y2) * rec->width);
    if (density < density_th)
    {
        return lsd_reduce_region_radius(reg, reg_size, modgrad, reg_angle, prec, p, rec, used, density_th);
    }
    return 1;
}

static double lsd_inter_low(double x, double x1, double y1, double x2, double y2)
{
    if (lsd_double_equal(x1, x2) && y1 < y2)
    {
        return y1;
    }
    if (lsd_double_equal(x1, x2) && y1 > y2)
    {
        return y2;
    }
    return y1 + (x - x1) * (y2 - y1) / (x2 - x1);
}

static double lsd_inter_high(double x, double x1, double y1, double x2, double y2)
{
    if (lsd_double_equal(x1, x2) && y1 < y2)
    {
        return y2;
    }
    if (lsd_double_equal(x1, x2) && y1 > y2)
    {
        return y1;
    }
    return y1 + (x - x1) * (y2 - y1) / (x2 - x1);
}

static void lsd_ri_inc(LsdRectIter *i)
{
    if ((double)i->x <= i->vx[2])
    {
        i->y++;
    }
    while ((double)i->y > i->ye && (double)i->x <= i->vx[2])
    {
        i->x++;
        if ((double)i->x > i->vx[2])
        {
            return;
        }
        if ((double)i->x < i->vx[3])
        {
            i->ys = lsd_inter_low((double)i->x, i->vx[0], i->vy[0], i->vx[3], i->vy[3]);
        }
        else
        {
            i->ys = lsd_inter_low((double)i->x, i->vx[3], i->vy[3], i->vx[2], i->vy[2]);
        }
        if ((double)i->x < i->vx[1])
        {
            i->ye = lsd_inter_high((double)i->x, i->vx[0], i->vy[0], i->vx[1], i->vy[1]);
        }
        else
        {
            i->ye = lsd_inter_high((double)i->x, i->vx[1], i->vy[1], i->vx[2], i->vy[2]);
        }
        i->y = (int)ceil(i->ys);
    }
}

static void lsd_ri_ini(const LsdRect *r, LsdRectIter *iter)
{
    double vx[4];
    double vy[4];
    unsigned int offset;
    unsigned int n;

    vx[0] = r->x1 - r->dy * r->width * 0.5;
    vy[0] = r->y1 + r->dx * r->width * 0.5;
    vx[1] = r->x2 - r->dy * r->width * 0.5;
    vy[1] = r->y2 + r->dx * r->width * 0.5;
    vx[2] = r->x2 + r->dy * r->width * 0.5;
    vy[2] = r->y2 - r->dx * r->width * 0.5;
    vx[3] = r->x1 + r->dy * r->width * 0.5;
    vy[3] = r->y1 - r->dx * r->width * 0.5;

    if (r->x1 < r->x2 && r->y1 <= r->y2)
    {
        offset = 0;
    }
    else if (r->x1 >= r->x2 && r->y1 < r->y2)
    {
        offset = 1;
    }
    else if (r->x1 > r->x2 && r->y1 >= r->y2)
    {
        offset = 2;
    }
    else
    {
        offset = 3;
    }

    for (n = 0; n < 4; n++)
    {
        iter->vx[n] = vx[(offset + n) % 4u];
        iter->vy[n] = vy[(offset + n) % 4u];
    }

    iter->x = (int)ceil(iter->vx[0]) - 1;
    iter->y = (int)ceil(iter->vy[0]);
    iter->ys = -LSD_DBL_MAX;
    iter->ye = -LSD_DBL_MAX;
    lsd_ri_inc(iter);
}

static double lsd_log_gamma(double x)
{
    static const double q[7] =
        {
            75122.6331530,
            80916.6278952,
            36308.2951477,
            8687.24529705,
            1168.92649479,
            83.8676043424,
            2.50662827511};
    double a;
    double b;
    int n;

    if (x > 15.0)
    {
        double xp = pow(x, 6.0);
        return 0.918938533204673 + (x - 0.5) * log(x) - x +
               0.5 * x * log(x * sinh(1.0 / x) + 1.0 / (810.0 * xp));
    }

    a = (x + 0.5) * log(x + 5.5) - (x + 5.5);
    b = 0.0;
    for (n = 0; n < 7; n++)
    {
        a -= log(x + (double)n);
        b += q[n] * pow(x, (double)n);
    }
    return a + log(b);
}

static double lsd_nfa(int n, int k, double p, double log_nt)
{
    double tolerance = 0.1;
    double log1term;
    double term;
    double bin_tail;
    double mult_term;
    int i;

    if (n == 0 || k == 0)
    {
        return -log_nt;
    }
    if (n == k)
    {
        return -log_nt - (double)n * log10((double)(float)p);
    }

    mult_term = p / (1.0 - (double)(float)p);
    log1term = lsd_log_gamma((double)((float)n + 1.0f)) -
               lsd_log_gamma((double)k + 1.0) -
               lsd_log_gamma((double)(n - k) + 1.0) +
               (double)k * log(p) + (double)(n - k) * log(1.0 - (double)(float)p);
    term = exp(log1term);

    if (lsd_double_equal(term, 0.0))
    {
        if ((double)k > (double)n * p)
        {
            return -log1term / 2.302585092994046 - log_nt;
        }
        return -log_nt;
    }

    bin_tail = term;
    for (i = k + 1; i <= n; i++)
    {
        double inv_i;
        double bin_term;
        double p_term;

        if (i < LSD_TABSIZE)
        {
            if (g_lsd_inv[i] == 0.0)
            {
                g_lsd_inv[i] = 1.0 / (double)i;
            }
            inv_i = g_lsd_inv[i];
        }
        else
        {
            inv_i = 1.0 / (double)i;
        }

        bin_term = (double)(n - i + 1) * inv_i;
        p_term = bin_term * mult_term;
        term *= p_term;
        bin_tail += term;
        if (bin_term < 1.0)
        {
            double err = term * ((1.0 - pow(p_term, (double)(n - i + 1))) / (1.0 - p_term) - 1.0);
            if (err < tolerance * fabs(-log10(bin_tail) - log_nt) * bin_tail)
            {
                break;
            }
        }
    }
    return -log10(bin_tail) - log_nt;
}

static double lsd_rect_nfa(const LsdRect *rec, const LsdImageDouble *angles, double log_nt)
{
    LsdRectIter iter;
    int pts = 0;
    int alg = 0;

    for (lsd_ri_ini(rec, &iter); (double)iter.x <= iter.vx[2]; lsd_ri_inc(&iter))
    {
        if (iter.x >= 0 && iter.y >= 0 &&
            iter.x < (int)angles->xsize && iter.y < (int)angles->ysize)
        {
            pts++;
            if (lsd_isaligned(iter.x, iter.y, angles, rec->theta, rec->prec))
            {
                alg++;
            }
        }
    }
    return lsd_nfa(pts, alg, rec->p, log_nt);
}

static double lsd_rect_improve(LsdRect *rec, const LsdImageDouble *angles, double log_nt, double log_eps)
{
    LsdRect r;
    double log_nfa;
    double log_nfa_new;
    double delta = 0.5;
    double delta_2 = delta * 0.5;
    int n;

    log_nfa = lsd_rect_nfa(rec, angles, log_nt);
    if (log_nfa > log_eps)
    {
        return log_nfa;
    }

    r = *rec;
    for (n = 0; n < 5; n++)
    {
        r.p *= 0.5;
        r.prec = r.p * LSD_PI;
        log_nfa_new = lsd_rect_nfa(&r, angles, log_nt);
        if (log_nfa_new > log_nfa)
        {
            log_nfa = log_nfa_new;
            *rec = r;
        }
    }
    if (log_nfa > log_eps)
    {
        return log_nfa;
    }

    r = *rec;
    for (n = 0; n < 5; n++)
    {
        if ((r.width - delta) >= 0.5)
        {
            r.width -= delta;
            log_nfa_new = lsd_rect_nfa(&r, angles, log_nt);
            if (log_nfa_new > log_nfa)
            {
                *rec = r;
                log_nfa = log_nfa_new;
            }
        }
    }
    if (log_nfa > log_eps)
    {
        return log_nfa;
    }

    r = *rec;
    for (n = 0; n < 5; n++)
    {
        if ((r.width - delta) >= 0.5)
        {
            r.x1 += -r.dy * delta_2;
            r.y1 += r.dx * delta_2;
            r.x2 += -r.dy * delta_2;
            r.y2 += r.dx * delta_2;
            r.width -= delta;
            log_nfa_new = lsd_rect_nfa(&r, angles, log_nt);
            if (log_nfa_new > log_nfa)
            {
                *rec = r;
                log_nfa = log_nfa_new;
            }
        }
    }
    if (log_nfa > log_eps)
    {
        return log_nfa;
    }

    r = *rec;
    for (n = 0; n < 5; n++)
    {
        if ((r.width - delta) >= 0.5)
        {
            r.x1 -= -r.dy * delta_2;
            r.y1 -= r.dx * delta_2;
            r.x2 -= -r.dy * delta_2;
            r.y2 -= r.dx * delta_2;
            r.width -= delta;
            log_nfa_new = lsd_rect_nfa(&r, angles, log_nt);
            if (log_nfa_new > log_nfa)
            {
                *rec = r;
                log_nfa = log_nfa_new;
            }
        }
    }
    if (log_nfa > log_eps)
    {
        return log_nfa;
    }

    r = *rec;
    for (n = 0; n < 5; n++)
    {
        r.p *= 0.5;
        r.prec = r.p * LSD_PI;
        log_nfa_new = lsd_rect_nfa(&r, angles, log_nt);
        if (log_nfa_new > log_nfa)
        {
            log_nfa = log_nfa_new;
            *rec = r;
        }
    }

    return log_nfa;
}

static double *lsd_scale_detect(int *n_out, double *img, int w, int h, double scale)
{
    LsdTupleList out;
    LsdImageDouble image;
    LsdImageDouble *scaled_image = NULL;
    LsdImageDouble *angles = NULL;
    LsdImageDouble *modgrad = NULL;
    LsdImageChar *used = NULL;
    LsdPoint *reg = NULL;
    LsdCoorList *list_p = NULL;
    void *mem_p = NULL;
    double prec = 0.39269908169872414;
    double p = 0.125;
    double rho = 5.226251859505506;
    double density_th = 0.7;
    double log_nt;
    int min_reg_size;

    *n_out = 0;
    if (!img || w < 1 || h < 1 || scale <= 0.0)
    {
        return NULL;
    }

    out.size = 0;
    out.max_size = 1;
    out.dim = 7;
    out.values = (double *)rm_malloc(7u * sizeof(double));
    if (!out.values)
    {
        return NULL;
    }

    image.data = img;
    image.xsize = (unsigned int)w;
    image.ysize = (unsigned int)h;

    if (scale != 1.0)
    {
        scaled_image = lsd_gaussian_sampler(&image, scale, 0.6);
        if (!scaled_image)
        {
            rm_free(out.values);
            return NULL;
        }
        angles = lsd_ll_angle(scaled_image, rho, &list_p, &mem_p, &modgrad, 1024u);
        lsd_free_image_double(scaled_image);
    }
    else
    {
        angles = lsd_ll_angle(&image, rho, &list_p, &mem_p, &modgrad, 1024u);
    }

    if (!angles)
    {
        rm_free(out.values);
        return NULL;
    }

    log_nt = 5.0 * (log10((double)angles->xsize) + log10((double)angles->ysize)) * 0.5 +
             1.0413926851582251;
    min_reg_size = (int)(-log_nt / -0.9030899869919435);

    used = lsd_new_image_char_ini(angles->xsize, angles->ysize, 0);
    reg = (LsdPoint *)rm_calloc((size_t)angles->xsize * (size_t)angles->ysize, sizeof(LsdPoint));
    if (!used || !reg)
    {
        lsd_free_image_char(used);
        rm_free(reg);
        lsd_free_image_double(angles);
        lsd_free_image_double(modgrad);
        rm_free(mem_p);
        rm_free(out.values);
        return NULL;
    }

    for (; list_p != NULL; list_p = list_p->next)
    {
        int reg_size = 0;
        double reg_angle = 0.0;
        LsdRect rec;
        double log_nfa;

        if (used->data[(size_t)list_p->y * used->xsize + (size_t)list_p->x] != 0 ||
            angles->data[(size_t)list_p->y * angles->xsize + (size_t)list_p->x] == LSD_NOTDEF)
        {
            continue;
        }

        lsd_region_grow(list_p->x, list_p->y, angles, reg, &reg_size, &reg_angle, used, prec);
        if (reg_size < min_reg_size)
        {
            continue;
        }

        lsd_region2rect(reg, reg_size, modgrad, reg_angle, prec, p, &rec);
        if (!lsd_refine(reg, &reg_size, modgrad, reg_angle, prec, p, &rec, used, angles, density_th))
        {
            continue;
        }

        log_nfa = lsd_rect_improve(&rec, angles, log_nt, 0.0);
        if (log_nfa <= 0.0)
        {
            continue;
        }

        rec.x1 += 0.5;
        rec.y1 += 0.5;
        rec.x2 += 0.5;
        rec.y2 += 0.5;
        if (scale != 1.0)
        {
            rec.x1 /= scale;
            rec.y1 /= scale;
            rec.x2 /= scale;
            rec.y2 /= scale;
            rec.width /= scale;
        }
        lsd_add_7tuple(&out, rec.x1, rec.y1, rec.x2, rec.y2, rec.width, rec.p, log_nfa);
    }

    lsd_free_image_double(angles);
    lsd_free_image_double(modgrad);
    lsd_free_image_char(used);
    rm_free(reg);
    rm_free(mem_p);

    *n_out = (int)out.size;
    return out.values;
}

static int run_linedetection(lua_State *L, const uint8_t *bgra, int w, int h)
{
    double *img;
    double *segs;
    double bg_luma = 0.0;
    double scale;
    int top;
    int centered = 0;
    int count = 0;
    int i;
    const uint8_t *p;

    img = (double *)rm_malloc((size_t)w * (size_t)h * sizeof(double));
    if (!img)
    {
        return 0;
    }

    top = lua_gettop(L);
    if (top >= 3)
    {
        if (lua_type(L, 3) == LUA_TBOOLEAN)
        {
            centered = lua_toboolean(L, 3);
        }
    }
    if (top >= 2)
    {
        if (lua_type(L, 2) == LUA_TNUMBER)
        {
            int col = (int)lua_tointeger(L, 2);
            if (col > 0)
            {
                bg_luma = (double)(col & 0xff) * 0.114 +
                          (double)((col >> 8) & 0xff) * 0.587 +
                          (double)((col >> 16) & 0xff) * 0.299;
            }
        }
    }

    p = bgra + (size_t)w * (size_t)h * 4u;
    for (i = w * h - 1; i >= 0; i--)
    {
        double luma = (double)p[-4] * 0.114 + (double)p[-3] * 0.587 + (double)p[-2] * 0.299;
        img[i] = ((double)p[-1] * (luma - bg_luma)) / 255.0 + bg_luma;
        p -= 4;
    }

    scale = lua_tonumber(L, 1) * 0.01;
    if (!(scale > 0.0))
    {
        scale = 0.8;
    }

    segs = lsd_scale_detect(&count, img, w, h, scale);
    if (count > 0 && segs != NULL)
    {
        double cx = centered ? (double)((float)w * 0.5f) : 0.0;
        double cy = centered ? (double)((float)h * 0.5f) : 0.0;
        const double *s = segs;

        lua_createtable(L, 0, 0);
        for (i = 0; i < count; i++)
        {
            lua_createtable(L, 0, 0);
            lua_pushnumber(L, s[0] - cx);
            lua_setfield(L, -2, "x0");
            lua_pushnumber(L, s[1] - cy);
            lua_setfield(L, -2, "y0");
            lua_pushnumber(L, s[2] - cx);
            lua_setfield(L, -2, "x1");
            lua_pushnumber(L, s[3] - cy);
            lua_setfield(L, -2, "y1");
            lua_rawseti(L, -2, i + 1);
            s += 7;
        }
    }

    rm_free(segs);
    rm_free(img);
    return count;
}

static int append_border_point(int **pts_ptr, int *cap_ptr, int *len_ptr, int idx)
{
    int len = *len_ptr;
    if (len >= *cap_ptr)
    {
        int new_cap = len + 100;
        int *grown = (int *)rm_realloc(*pts_ptr, (size_t)new_cap * sizeof(int));
        if (!grown)
        {
            return 0;
        }
        *pts_ptr = grown;
        *cap_ptr = new_cap;
    }
    (*pts_ptr)[len] = idx;
    *len_ptr = len + 1;
    return 1;
}

int __cdecl lua_rikky_bordering(lua_State *L)
{
    int skip;
    int step;
    uint8_t alpha_th = 0;
    int use_zoom = 0;
    int use_rot = 0;
    int connect_4 = 0;
    double scale = 1.0;
    const uint8_t *bgra;
    int w;
    int h;
    int count;
    uint8_t *alpha = NULL;
    uint8_t *edge = NULL;
    int *pts = NULL;
    int pts_cap;
    int contour_count = 0;
    int t;
    int i;

    t = lua_type(L, 1);
    if (t == LUA_TNUMBER)
    {
        skip = (int)lua_tointeger(L, 1);
        if (skip > 5000)
        {
            skip = 5000;
        }
        if (skip < 0)
        {
            skip = 0;
        }
        step = skip + 1;
    }
    else if (t == LUA_TSTRING)
    {
        const char *s = lua_tolstring(L, 1, NULL);
        step = 1;
        skip = (s && strcmp(s, "pixel") == 0) ? -1 : 0;
    }
    else
    {
        step = 1;
        skip = 0;
    }

    if (lua_type(L, 2) == LUA_TNUMBER)
    {
        alpha_th = (uint8_t)(int16_t)(lua_tonumber(L, 2) * 2.55);
        if (alpha_th == 0xffu)
        {
            alpha_th = 0xfeu;
        }
    }

    t = lua_type(L, 3);
    if (t == LUA_TBOOLEAN)
    {
        use_zoom = lua_toboolean(L, 3);
    }
    else if (t == LUA_TNUMBER)
    {
        use_zoom = ((int8_t)lua_tointeger(L, 3) == 1) ? 1 : 0;
    }

    t = lua_type(L, 4);
    if (t == LUA_TBOOLEAN)
    {
        use_rot = lua_toboolean(L, 4);
    }
    else if (t == LUA_TNUMBER)
    {
        use_rot = ((int8_t)lua_tointeger(L, 4) == 1) ? 1 : 0;
    }

    t = lua_type(L, 5);
    if (t == LUA_TBOOLEAN)
    {
        connect_4 = lua_toboolean(L, 5);
    }
    else if (t == LUA_TNUMBER)
    {
        connect_4 = ((int8_t)lua_tointeger(L, 5) == 1) ? 1 : 0;
    }

    lua_settop(L, 0);
    lua_getglobal(L, "obj");

    if (use_zoom)
    {
        double z1;
        double z2;
        lua_getfield(L, 1, "getvalue");
        lua_pushstring(L, "zoom");
        lua_call(L, 1, 1);
        lua_getfield(L, 1, "zoom");
        z1 = lua_tonumber(L, -2);
        z2 = lua_tonumber(L, -1);
        scale = z1 * z2 * 0.01;
    }

    if (use_rot)
    {
        const double deg2rad = 0.017453292519943295;
        double rx;
        double ry;
        double rz;
        lua_getfield(L, 1, "rx");
        lua_getfield(L, 1, "ry");
        lua_getfield(L, 1, "rz");
        rz = lua_tonumber(L, -1);
        ry = lua_tonumber(L, -2);
        rx = lua_tonumber(L, -3);
        rm_euler_set_trig(rx * deg2rad, ry * deg2rad, rz * deg2rad);
    }

    lua_getfield(L, 1, "getpixeldata");
    lua_call(L, 0, 3);
    bgra = (const uint8_t *)lua_touserdata(L, -3);
    w = (int)lua_tointeger(L, -2);
    h = (int)lua_tointeger(L, -1);
    lua_settop(L, 0);

    if (w == 0 || h == 0 || !bgra)
    {
        lua_createtable(L, 0, 0);
        lua_createtable(L, 0, 0);
        lua_pushinteger(L, 0);
        return 3;
    }

    count = w * h;
    alpha = (uint8_t *)rm_calloc((size_t)count, 1);
    edge = (uint8_t *)rm_calloc((size_t)count, 1);
    pts = (int *)rm_calloc((size_t)count, sizeof(int));
    if (!alpha || !edge || !pts)
    {
        rm_free(alpha);
        rm_free(edge);
        rm_free(pts);
        lua_createtable(L, 0, 0);
        lua_createtable(L, 0, 0);
        lua_pushinteger(L, 0);
        return 3;
    }
    pts_cap = count;

    for (i = 0; i < count; i++)
    {
        alpha[i] = bgra[(size_t)i * 4u + 3u];
    }

    if (alpha[0] > alpha_th)
    {
        edge[0] = 1;
    }
    if (alpha[w - 1] > alpha_th)
    {
        edge[w - 1] = 1;
    }
    if (alpha[count - w] > alpha_th)
    {
        edge[count - w] = 1;
    }
    if (alpha[count - 1] > alpha_th)
    {
        edge[count - 1] = 1;
    }

    for (i = 1; i < w - 1; i++)
    {
        if (alpha[i] > alpha_th)
        {
            edge[i] = 1;
        }
        if (alpha[count - w + i] > alpha_th)
        {
            edge[count - w + i] = 1;
        }
    }

    for (i = 1; i < h - 1; i++)
    {
        int row = i * w;
        if (alpha[row] > alpha_th)
        {
            edge[row] = 1;
        }
        if (alpha[row + w - 1] > alpha_th)
        {
            edge[row + w - 1] = 1;
        }
    }

    for (i = 1; i < h - 1; i++)
    {
        int row = i * w;
        int x;
        for (x = 1; x < w - 1; x++)
        {
            int idx = row + x;
            if (alpha[idx] > alpha_th &&
                (alpha[idx + 1] <= alpha_th || alpha[idx - 1] <= alpha_th ||
                 alpha[idx + w] <= alpha_th || alpha[idx - w] <= alpha_th))
            {
                edge[idx] = 1;
            }
        }
    }

    lua_createtable(L, 0, 0);
    lua_createtable(L, 0, 0);

    if (count > 0)
    {
        double ox = (double)(0.5f - (float)w * 0.5f);
        double oy = (double)(0.5f - (float)h * 0.5f);

        for (i = 0; i < count; i++)
        {
            if (edge[i] != 0)
            {
                div_t start_d = div(i, w);
                int start_x = start_d.rem;
                int start_y = start_d.quot;
                int x = start_x;
                int y = start_y;
                int idx = i;
                int dir = ((i - w) < 0 || bgra[(i - w) * 4 + 3] <= alpha_th) ? 2 : 4;
                int n = 1;
                int tries = 0;
                int k;

                pts[0] = i;

                for (;;)
                {
                    int nx = x;
                    int ny = y;
                    int nidx = idx;
                    int next_dir = dir;
                    int moved = 0;

                    switch (dir)
                    {
                    case 0:
                        if (y != 0 && x != 0 && edge[idx - w - 1] == 1)
                        {
                            if (connect_4 == 1 && alpha[idx - 1] > alpha_th)
                            {
                                nx = x - 1;
                                ny = y;
                                nidx = idx - 1;
                                next_dir = 7;
                            }
                            else
                            {
                                nx = x - 1;
                                ny = y - 1;
                                nidx = idx - w - 1;
                                next_dir = 6;
                            }
                            moved = 1;
                            break;
                        }
                        tries++;
                        // fall through
                    case 1:
                        if (x != 0 && edge[idx - 1] == 1)
                        {
                            nx = x - 1;
                            ny = y;
                            nidx = idx - 1;
                            next_dir = 0;
                            moved = 1;
                            break;
                        }
                        tries++;
                        // fall through
                    case 2:
                        if (y != h - 1 && x != 0 && edge[idx + w - 1] == 1)
                        {
                            if (connect_4 == 1 && alpha[idx + w] > alpha_th)
                            {
                                nx = x;
                                ny = y + 1;
                                nidx = idx + w;
                                next_dir = 1;
                            }
                            else
                            {
                                nx = x - 1;
                                ny = y + 1;
                                nidx = idx + w - 1;
                                next_dir = 0;
                            }
                            moved = 1;
                            break;
                        }
                        tries++;
                        // fall through
                    case 3:
                        if (y != h - 1 && edge[idx + w] == 1)
                        {
                            nx = x;
                            ny = y + 1;
                            nidx = idx + w;
                            next_dir = 2;
                            moved = 1;
                            break;
                        }
                        tries++;
                        // fall through
                    case 4:
                        if (y != h - 1 && x != w - 1 && edge[idx + w + 1] == 1)
                        {
                            if (connect_4 == 1 && alpha[idx + 1] > alpha_th)
                            {
                                nx = x + 1;
                                ny = y;
                                nidx = idx + 1;
                                next_dir = 3;
                            }
                            else
                            {
                                nx = x + 1;
                                ny = y + 1;
                                nidx = idx + w + 1;
                                next_dir = 2;
                            }
                            moved = 1;
                            break;
                        }
                        tries++;
                        // fall through
                    case 5:
                        if (x != w - 1 && edge[idx + 1] == 1)
                        {
                            nx = x + 1;
                            ny = y;
                            nidx = idx + 1;
                            next_dir = 4;
                            moved = 1;
                            break;
                        }
                        tries++;
                        // fall through
                    case 6:
                        if (y != 0 && x != w - 1 && edge[idx - w + 1] == 1)
                        {
                            if (connect_4 == 1 && alpha[idx - w] > alpha_th)
                            {
                                nx = x;
                                ny = y - 1;
                                nidx = idx - w;
                                next_dir = 5;
                            }
                            else
                            {
                                nx = x + 1;
                                ny = y - 1;
                                nidx = idx - w + 1;
                                next_dir = 4;
                            }
                            moved = 1;
                            break;
                        }
                        tries++;
                        // fall through
                    default:
                        if (y != 0 && edge[idx - w] == 1)
                        {
                            nx = x;
                            ny = y - 1;
                            nidx = idx - w;
                            next_dir = 6;
                            moved = 1;
                            break;
                        }
                        tries++;
                        if (tries > 7)
                        {
                            moved = -1;
                        }
                        else
                        {
                            dir = 0;
                        }
                        break;
                    }

                    if (moved < 0)
                    {
                        break;
                    }
                    if (moved > 0)
                    {
                        if (nx == start_x && ny == start_y)
                        {
                            break;
                        }
                        if (!append_border_point(&pts, &pts_cap, &n, nidx))
                        {
                            rm_free(alpha);
                            rm_free(edge);
                            rm_free(pts);
                            lua_settop(L, 0);
                            lua_createtable(L, 0, 0);
                            lua_createtable(L, 0, 0);
                            lua_pushinteger(L, 0);
                            return 3;
                        }
                        x = nx;
                        y = ny;
                        idx = nidx;
                        dir = next_dir;
                        tries = 0;
                    }
                }

                if (n > 4)
                {
                    int out_pts = 0;
                    int ti = 0;

                    lua_createtable(L, 0, 0);
                    if (skip == -1)
                    {
                        for (i = 0; i < n; i += step)
                        {
                            div_t d = div(pts[i], w);
                            lua_pushinteger(L, d.rem);
                            lua_rawseti(L, -2, ti + 1);
                            lua_pushinteger(L, d.quot);
                            lua_rawseti(L, -2, ti + 2);
                            ti += 2;
                        }
                        out_pts = ti >> 1;
                    }
                    else if (!use_zoom)
                    {
                        if (use_rot)
                        {
                            for (i = 0; i < n; i += step)
                            {
                                div_t d = div(pts[i], w);
                                double vin[3];
                                double vout[3];
                                vin[0] = (double)d.rem + ox;
                                vin[1] = (double)d.quot + oy;
                                vin[2] = 0.0;
                                rm_euler_rotate_vec3(vout, vin);
                                lua_pushnumber(L, vout[0]);
                                lua_rawseti(L, -2, ti + 1);
                                lua_pushnumber(L, vout[1]);
                                lua_rawseti(L, -2, ti + 2);
                                lua_pushnumber(L, vout[2]);
                                lua_rawseti(L, -2, ti + 3);
                                ti += 3;
                            }
                            out_pts = ti / 3;
                        }
                        else
                        {
                            for (i = 0; i < n; i += step)
                            {
                                div_t d = div(pts[i], w);
                                lua_pushnumber(L, (double)d.rem + ox);
                                lua_rawseti(L, -2, ti + 1);
                                lua_pushnumber(L, (double)d.quot + oy);
                                lua_rawseti(L, -2, ti + 2);
                                ti += 2;
                            }
                            out_pts = ti >> 1;
                        }
                    }
                    else if (!use_rot)
                    {
                        for (i = 0; i < n; i += step)
                        {
                            div_t d = div(pts[i], w);
                            lua_pushnumber(L, ((double)d.rem + ox) * scale);
                            lua_rawseti(L, -2, ti + 1);
                            lua_pushnumber(L, ((double)d.quot + oy) * scale);
                            lua_rawseti(L, -2, ti + 2);
                            ti += 2;
                        }
                        out_pts = ti >> 1;
                    }
                    else
                    {
                        for (i = 0; i < n; i += step)
                        {
                            div_t d = div(pts[i], w);
                            double vin[3];
                            double vout[3];
                            vin[0] = ((double)d.rem + ox) * scale;
                            vin[1] = ((double)d.quot + oy) * scale;
                            vin[2] = 0.0;
                            rm_euler_rotate_vec3(vout, vin);
                            lua_pushnumber(L, vout[0]);
                            lua_rawseti(L, -2, ti + 1);
                            lua_pushnumber(L, vout[1]);
                            lua_rawseti(L, -2, ti + 2);
                            lua_pushnumber(L, vout[2]);
                            lua_rawseti(L, -2, ti + 3);
                            ti += 3;
                        }
                        out_pts = ti / 3;
                    }

                    contour_count++;
                    lua_rawseti(L, -3, contour_count);
                    lua_pushinteger(L, out_pts);
                    lua_rawseti(L, -2, contour_count);
                }

                for (k = 0; k < n; k++)
                {
                    edge[pts[k]] = 0;
                }
            }
        }
    }

    lua_pushnumber(L, (double)contour_count);
    rm_free(alpha);
    rm_free(edge);
    rm_free(pts);
    return 3;
}

static uint32_t pack_lua_rgba_return(lua_State *L)
{
    uint32_t a = (uint32_t)lua_tointeger(L, -1);
    uint32_t r = (uint32_t)lua_tointeger(L, -4) & 0xffu;
    uint32_t g = (uint32_t)lua_tointeger(L, -3) & 0xffu;
    uint32_t b = (uint32_t)lua_tointeger(L, -2) & 0xffu;
    return b | (g << 8) | (r << 16) | (a << 24);
}

int __cdecl lua_rikky_pixelfunction(lua_State *L)
{
    int t2;
    const uint32_t *src;
    uint32_t *dst;
    int w;
    int h;
    int total;
    int idx;
    int x;
    int y;

    if (lua_type(L, 1) != LUA_TFUNCTION)
    {
        return 0;
    }

    t2 = lua_type(L, 2);
    if (t2 == LUA_TLIGHTUSERDATA)
    {
        const uint32_t *src2;

        lua_getglobal(L, "obj");
        lua_getfield(L, -1, "getpixeldata");
        lua_call(L, 0, 3);
        src = (const uint32_t *)lua_touserdata(L, -3);
        w = (int)lua_tointeger(L, -2);
        h = (int)lua_tointeger(L, -1);
        lua_pop(L, 3);

        total = w * h;
        dst = (uint32_t *)rm_malloc((size_t)total * sizeof(uint32_t));
        if (!dst)
        {
            return 0;
        }

        src2 = (const uint32_t *)lua_touserdata(L, 2);
        y = h - 1;
        x = w - 1;
        for (idx = total - 1; idx >= 0; idx--)
        {
            uint32_t p1 = src[idx];
            uint32_t p2 = src2[idx];

            lua_pushvalue(L, 1);
            lua_pushinteger(L, (lua_Integer)((p1 >> 16) & 0xffu));
            lua_pushinteger(L, (lua_Integer)((p1 >> 8) & 0xffu));
            lua_pushinteger(L, (lua_Integer)(p1 & 0xffu));
            lua_pushinteger(L, (lua_Integer)(p1 >> 24));
            lua_pushinteger(L, (lua_Integer)((p2 >> 16) & 0xffu));
            lua_pushinteger(L, (lua_Integer)((p2 >> 8) & 0xffu));
            lua_pushinteger(L, (lua_Integer)(p2 & 0xffu));
            lua_pushinteger(L, (lua_Integer)(p2 >> 24));
            lua_pushinteger(L, x);
            lua_pushinteger(L, y);
            lua_call(L, 10, 4);
            dst[idx] = pack_lua_rgba_return(L);
            lua_pop(L, 4);

            x--;
            if (x == -1)
            {
                y--;
                x = w - 1;
            }
        }
    }
    else if (t2 == -1 || t2 == LUA_TNIL)
    {
        lua_getglobal(L, "obj");
        lua_getfield(L, -1, "getpixeldata");
        lua_call(L, 0, 3);
        src = (const uint32_t *)lua_touserdata(L, -3);
        w = (int)lua_tointeger(L, -2);
        h = (int)lua_tointeger(L, -1);
        lua_pop(L, 3);

        total = w * h;
        dst = (uint32_t *)rm_malloc((size_t)total * sizeof(uint32_t));
        if (!dst)
        {
            return 0;
        }

        y = h - 1;
        x = w - 1;
        for (idx = total - 1; idx >= 0; idx--)
        {
            uint32_t p1 = src[idx];

            lua_pushvalue(L, 1);
            lua_pushinteger(L, (lua_Integer)((p1 >> 16) & 0xffu));
            lua_pushinteger(L, (lua_Integer)((p1 >> 8) & 0xffu));
            lua_pushinteger(L, (lua_Integer)(p1 & 0xffu));
            lua_pushinteger(L, (lua_Integer)(p1 >> 24));
            lua_pushinteger(L, 0);
            lua_pushinteger(L, 0);
            lua_pushinteger(L, 0);
            lua_pushinteger(L, 0);
            lua_pushinteger(L, x);
            lua_pushinteger(L, y);
            lua_call(L, 10, 4);
            dst[idx] = pack_lua_rgba_return(L);
            lua_pop(L, 4);

            x--;
            if (x == -1)
            {
                y--;
                x = w - 1;
            }
        }
    }
    else if (t2 == LUA_TTABLE)
    {
        int t0;

        lua_getglobal(L, "obj");
        lua_getfield(L, -1, "getpixeldata");
        lua_call(L, 0, 3);
        src = (const uint32_t *)lua_touserdata(L, -3);
        w = (int)lua_tointeger(L, -2);
        h = (int)lua_tointeger(L, -1);
        total = w * h;
        lua_pop(L, 3);

        dst = (uint32_t *)rm_malloc((size_t)total * sizeof(uint32_t));
        if (!dst)
        {
            return 0;
        }

        lua_rawgeti(L, 2, 0);
        t0 = lua_type(L, -1);
        if (t0 == LUA_TNIL)
        {
            lua_rawgeti(L, 2, 1);
            if (lua_type(L, -1) != LUA_TTABLE)
            {
                rm_free(dst);
                return 0;
            }
            lua_getfield(L, -1, "r");
            if (lua_type(L, -1) == LUA_TNUMBER)
            {
                int k;
                lua_pop(L, 3);
                y = h;
                x = w;
                for (k = total; k > 0; k--)
                {
                    uint32_t p1 = src[k - 1];

                    lua_rawgeti(L, 2, k);
                    lua_pushvalue(L, 1);
                    lua_pushinteger(L, (lua_Integer)((p1 >> 16) & 0xffu));
                    lua_pushinteger(L, (lua_Integer)((p1 >> 8) & 0xffu));
                    lua_pushinteger(L, (lua_Integer)(p1 & 0xffu));
                    lua_pushinteger(L, (lua_Integer)(p1 >> 24));
                    lua_getfield(L, -6, "r");
                    lua_getfield(L, -7, "g");
                    lua_getfield(L, -8, "b");
                    lua_getfield(L, -9, "a");
                    lua_pushinteger(L, x);
                    lua_pushinteger(L, y);
                    lua_call(L, 10, 4);
                    dst[k - 1] = pack_lua_rgba_return(L);
                    lua_pop(L, 5);

                    x--;
                    if (x == 0)
                    {
                        y--;
                        x = w;
                    }
                }
            }
            else
            {
                lua_rawgeti(L, -2, 1);
                if (lua_type(L, -1) != LUA_TTABLE)
                {
                    rm_free(dst);
                    return 0;
                }
                lua_getfield(L, -1, "r");
                if (lua_type(L, -1) != LUA_TNUMBER)
                {
                    rm_free(dst);
                    return 0;
                }
                lua_pop(L, 5);

                y = h;
                lua_rawgeti(L, 2, y);
                x = w;
                for (idx = total - 1; idx >= 0; idx--)
                {
                    uint32_t p1 = src[idx];

                    lua_rawgeti(L, -1, x);
                    lua_pushvalue(L, 1);
                    lua_pushinteger(L, (lua_Integer)((p1 >> 16) & 0xffu));
                    lua_pushinteger(L, (lua_Integer)((p1 >> 8) & 0xffu));
                    lua_pushinteger(L, (lua_Integer)(p1 & 0xffu));
                    lua_pushinteger(L, (lua_Integer)(p1 >> 24));
                    lua_getfield(L, -6, "r");
                    lua_getfield(L, -7, "g");
                    lua_getfield(L, -8, "b");
                    lua_getfield(L, -9, "a");
                    lua_pushinteger(L, x);
                    lua_pushinteger(L, y);
                    lua_call(L, 10, 4);
                    dst[idx] = pack_lua_rgba_return(L);
                    lua_pop(L, 5);

                    x--;
                    if (x == 0)
                    {
                        y--;
                        lua_pop(L, 1);
                        lua_rawgeti(L, 2, y);
                        x = w;
                    }
                }
                lua_pop(L, 1);
            }
        }
        else if (t0 == LUA_TTABLE)
        {
            lua_getfield(L, -1, "r");
            if (lua_type(L, -1) == LUA_TNUMBER)
            {
                lua_pop(L, 2);
                y = h - 1;
                x = w - 1;
                for (idx = total - 1; idx >= 0; idx--)
                {
                    uint32_t p1 = src[idx];

                    lua_rawgeti(L, 2, idx);
                    lua_pushvalue(L, 1);
                    lua_pushinteger(L, (lua_Integer)((p1 >> 16) & 0xffu));
                    lua_pushinteger(L, (lua_Integer)((p1 >> 8) & 0xffu));
                    lua_pushinteger(L, (lua_Integer)(p1 & 0xffu));
                    lua_pushinteger(L, (lua_Integer)(p1 >> 24));
                    lua_getfield(L, -6, "r");
                    lua_getfield(L, -7, "g");
                    lua_getfield(L, -8, "b");
                    lua_getfield(L, -9, "a");
                    lua_pushinteger(L, x);
                    lua_pushinteger(L, y);
                    lua_call(L, 10, 4);
                    dst[idx] = pack_lua_rgba_return(L);
                    lua_pop(L, 5);

                    x--;
                    if (x == -1)
                    {
                        y--;
                        x = w - 1;
                    }
                }
            }
            else
            {
                lua_rawgeti(L, -2, 0);
                if (lua_type(L, -1) != LUA_TTABLE)
                {
                    rm_free(dst);
                    return 0;
                }
                lua_getfield(L, -1, "r");
                if (lua_type(L, -1) != LUA_TNUMBER)
                {
                    rm_free(dst);
                    return 0;
                }
                lua_pop(L, 4);

                y = h - 1;
                lua_rawgeti(L, 2, y);
                x = w - 1;
                for (idx = total - 1; idx >= 0; idx--)
                {
                    uint32_t p1 = src[idx];

                    lua_rawgeti(L, -1, x);
                    lua_pushvalue(L, 1);
                    lua_pushinteger(L, (lua_Integer)((p1 >> 16) & 0xffu));
                    lua_pushinteger(L, (lua_Integer)((p1 >> 8) & 0xffu));
                    lua_pushinteger(L, (lua_Integer)(p1 & 0xffu));
                    lua_pushinteger(L, (lua_Integer)(p1 >> 24));
                    lua_getfield(L, -6, "r");
                    lua_getfield(L, -7, "g");
                    lua_getfield(L, -8, "b");
                    lua_getfield(L, -9, "a");
                    lua_pushinteger(L, x);
                    lua_pushinteger(L, y);
                    lua_call(L, 10, 4);
                    dst[idx] = pack_lua_rgba_return(L);
                    lua_pop(L, 5);

                    x--;
                    if (x == -1)
                    {
                        y--;
                        lua_pop(L, 1);
                        lua_rawgeti(L, 2, y);
                        x = w - 1;
                    }
                }
                lua_pop(L, 1);
            }
        }
        else
        {
            rm_free(dst);
            return 0;
        }
    }
    else
    {
        return 0;
    }

    lua_getfield(L, -1, "putpixeldata");
    lua_pushlightuserdata(L, dst);
    lua_call(L, 1, 0);
    rm_free(dst);
    return 0;
}

int __cdecl lua_rikky_fillarea(lua_State *L)
{
    uint8_t *bgra;
    uint8_t *mask;
    int w = 0;
    int h = 0;
    int seed_x;
    int seed_y;
    int mode;
    uint32_t threshold;
    int bbox[4];
    int i;

    bgra = rm_read_exedit_bgra(L, 0, &w, &h);
    if (!bgra)
    {
        lua_pushboolean(L, 0);
        return 1;
    }

    seed_x = (int)lua_tointeger(L, 1);
    if (seed_x < 0)
    {
        seed_x = 0;
    }
    else if (seed_x >= w)
    {
        seed_x = w - 1;
    }

    seed_y = (int)lua_tointeger(L, 2);
    if (seed_y < 0)
    {
        seed_y = 0;
    }
    else if (seed_y >= h)
    {
        seed_y = h - 1;
    }

    mode = (int)lua_tointeger(L, 3);
    if (mode > 24)
    {
        mode = 24;
    }
    if (mode < 0)
    {
        mode = 0;
    }

    threshold = (uint32_t)lua_tointeger(L, 4);

    mask = (uint8_t *)FileArea(bgra, w, h, seed_x, seed_y, mode, threshold, NULL, bbox);
    if (!mask)
    {
        lua_pushboolean(L, 0);
        rm_free(bgra);
        return 1;
    }

    for (i = w * h - 1; i >= 0; i--)
    {
        if (mask[i] != 0)
        {
            ((uint32_t *)bgra)[i] = 0xffffffffu;
        }
        else
        {
            bgra[(size_t)i * 4u + 3u] = 0;
        }
    }
    rm_free(mask);

    lua_getglobal(L, "obj");
    lua_getfield(L, -1, "putpixeldata");
    lua_pushlightuserdata(L, bgra);
    lua_call(L, 1, 0);

    lua_pushinteger(L, bbox[0]);
    lua_pushinteger(L, bbox[1]);
    lua_pushinteger(L, bbox[2]);
    lua_pushinteger(L, bbox[3]);
    rm_free(bgra);
    return 4;
}

int __cdecl lua_rikky_linedetection(lua_State *L)
{
    uint8_t *bgra;
    int w = 0;
    int h = 0;

    bgra = rm_read_exedit_bgra(L, 0, &w, &h);
    if (bgra)
    {
        int count = run_linedetection(L, bgra, w, h);
        rm_free(bgra);
        if (count != 0)
        {
            lua_pushinteger(L, count);
            return 2;
        }
    }
    lua_pushboolean(L, 0);
    return 1;
}
