#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <string.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include <wlr/backend/session.h>

#include <kywc/log.h>

#include "config_p.h"
#include "output.h"
#include "server.h"

static const char *service_bus = "org.kde.KWin";
static const char *service_path = "/ColorCorrect";
static const char *service_interface = "org.kde.kwin.ColorCorrect";

#define KDE_METHOD(name, type, result, method) SD_BUS_METHOD(name, type, result, method, 0)

#define KDE_PROPERTY(name, type, read)                                                             \
    SD_BUS_PROPERTY(name, type, read, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE)

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define CLAMP(val, min, max) ((val) < (min) ? (min) : ((val) > (max) ? (max) : (val)))

#define QUICK_UPDATE_DURATION 2000
#define COLORTEMPE_STEP 50

#define MINI_CHUNK_SIZE 256

#define M_PI 3.14159265358979323846 /* pi */
#define RAD(x) ((x) * (M_PI / 180))
#define DEG(x) ((x) * (180 / M_PI))

/* Model of atmospheric refraction near horizon (in degrees). */
//#define SOLAR_ATM_REFRAC 0.833
//#define SOLAR_DAYTIME_ELEV (0.0 - SOLAR_ATM_REFRAC)
#define SOLAR_CIVIL_TWILIGHT_ELEV -6.0
#define SOLAR_SUN_CIVIL_HIGH 2

#define SOLOR_TIME_CIVIL_DAWN RAD(-90.0 + SOLAR_CIVIL_TWILIGHT_ELEV)
//#define SOLOR_TIME_SUNRISE RAD(-90.0 + SOLAR_DAYTIME_ELEV)
#define SOLOR_TIME_SUNRISE_CIVIL_HIGH RAD(-90.0 + SOLAR_SUN_CIVIL_HIGH)
#define SOLOR_TIME_CIVIL_DUSK RAD(90.0 - SOLAR_CIVIL_TWILIGHT_ELEV)
//#define SOLOR_TIME_SUNSET RAD(90.0 - SOLAR_DAYTIME_ELEV)
#define SOLOR_TIME_SUNSET_CIVIL_HIGH RAD(90.0 - SOLAR_SUN_CIVIL_HIGH)

enum nightcolor_mode {
    /* Color temperature is computed based on the current position of the Sun.
     * Location of the user is provided by Plasma.
     */
    NIGHTCOLOR_MODE_AUTOMATIC,

    /* Color temperature is computed based on the current position of the Sun.
     * Location of the user is provided by themselves.
     */
    NIGHTCOLOR_MODE_LOCATION,

    /* Color temperature is computed based on the current time.
     * Sunrise and sunset times have to be specified by the user.
     */
    NIGHTCOLOR_MODE_TIMINGS,

    /* Color temperature is constant thoughout the day. */
    NIGHTCOLOR_MODE_CONSTANT,
};

enum change_property {
    PROP_INHIBIT,
    PROP_ENABLE,
    PROP_RUNNING,
    PROP_MODE,
    PROP_TARGET_COLORTEMPE,
    PROP_CURRENT_COLORTEMPE,
    PROP_PREV_TRANS_TIMING,
    PROP_SCHED_TRANS_TIMING,
};

struct date_time {
    time_t begin;
    time_t end;
};

struct nigcolor_configs {
    /* specifies whether night color is enabled */
    bool active;

    enum nightcolor_mode mode;

    int32_t night_colortempe;
    int32_t day_colortempe;

    /* auto location provided by work space */
    double latitude_auto;
    double longitude_auto;
    /* manual location from config */
    double latitude_fixed;
    double longitude_fixed;

    uint32_t morning_begin_fixed;
    uint32_t evening_begin_fixed;

    /* minutes */
    uint32_t transition_time;
};

struct kde_output {
    struct kywc_output *kywc_output;
    struct wl_list link;
    struct wl_listener destroy;
    struct wl_listener on;
};

static struct kde_nightcolor_manager {
    struct config *config;

    /* listen clock change */
    struct wl_event_source *clockskew;
    struct wl_event_source *adjust_timer;
    struct wl_event_source *slow_update_start_timer;

    struct wl_list outputs;

    struct nigcolor_configs configs;
    /* specifies whether night color is currently running */
    bool running;
    /* whether it is currently day or night */
    bool daylight;

    bool initial;

    bool slow_update_starting;
    bool slow_adjusting;
    uint32_t adjust_timeout;

    uint32_t morning_time;
    uint32_t evening_time;

    int inhibit_refer_count;

    int current_colortempe;
    int target_colortempe;

    /* the previous and next sunrise/sunset intervals - in utc time */
    struct date_time prev_dtime;
    struct date_time next_dtime;

    struct wl_listener server_suspend;
    struct wl_listener server_resume;
    struct wl_listener session_active;

    struct wl_listener new_output;
    struct wl_listener destroy;
} *manager = NULL;

struct key_info {
    struct wl_list link;
    char *key;
    char *value;
};

static bool get_group_configs(struct wl_list *configs_list, const char *group, const char *filename)
{
    FILE *fp;
    char strline[MINI_CHUNK_SIZE];

    if ((fp = fopen(filename, "r")) == NULL) {
        kywc_log(KYWC_WARN, "have no such file:%s", filename);
        return false;
    }

    bool match_group = false;
    while (fgets(strline, sizeof(strline), fp)) {
        strline[MINI_CHUNK_SIZE - 1] = '\0';

        if (strlen(strline) < 1 || strline[0] == '\n') {
            continue;
        } else if (!match_group && strline[0] == '[') {
            /* group header */
            strline[strlen(strline) - 2] = '\0'; /* remove ']\n' */
            if (strcmp(strline + 1, group) == 0) {
                match_group = true;
            }
            continue;
        } else if (strline[0] == '[') {
            goto ret;
        }
        /* match group to parse value */
        if (match_group) {
            char *str_end = strline + strlen(strline) - 1;
            str_end[0] = str_end[0] == '\n' ? '\0' : str_end[0];

            char *value = NULL;
            char *key = strtok_r(strline, "=", &value);
            struct key_info *keyinfo = calloc(1, sizeof(*keyinfo));
            keyinfo->key = strdup(key);
            keyinfo->value = strdup(value);
            wl_list_insert(configs_list, &keyinfo->link);
        }
    }
ret:
    fclose(fp);

    return true;
}

static void configs_init(struct nigcolor_configs *configs)
{
    configs->active = false;
    configs->mode = NIGHTCOLOR_MODE_AUTOMATIC;
    configs->day_colortempe = 6500;
    configs->night_colortempe = 4500;
    configs->latitude_auto = 0.0;
    configs->longitude_auto = 0.0;
    configs->latitude_fixed = 0.0;
    configs->longitude_fixed = 0.0;
    configs->morning_begin_fixed = 600;
    configs->evening_begin_fixed = 1800;
    configs->transition_time = 30;
}

static bool read_nightcolor_configs(struct nigcolor_configs *configs)
{
    /* get config path */
    const char *home = getenv("HOME");
    char *config_home_fallback = NULL;

    const char *config_home = getenv("XDG_CONFIG_HOME");
    if ((!config_home || config_home[0] == '\0') && home) {
        size_t size_fallback = 1 + strlen(home) + strlen("/.config");
        config_home_fallback = calloc(size_fallback, sizeof(char));
        if (config_home_fallback != NULL) {
            snprintf(config_home_fallback, size_fallback, "%s/.config", home);
        }
        config_home = config_home_fallback;
    }

    const char *filename = "kwinrc";
    size_t size = 2 + strlen(config_home) + strlen(filename);
    char *path = calloc(size, sizeof(char));
    if (!path) {
        return false;
    }
    snprintf(path, size, "%s/%s", config_home, filename);
    free(config_home_fallback);

    configs_init(configs);

    /* read configs */
    struct wl_list list_configs;
    wl_list_init(&list_configs);
    if (!get_group_configs(&list_configs, "NightColor", path)) {
        free(path);
        return false;
    }
    free(path);

    struct key_info *keyinfo, *tmp;
    wl_list_for_each_safe(keyinfo, tmp, &list_configs, link) {
        if (strcmp(keyinfo->key, "Mode") == 0) {
            if (strcmp(keyinfo->value, "Automatic") == 0) {
                configs->mode = NIGHTCOLOR_MODE_AUTOMATIC;
            } else if (strcmp(keyinfo->value, "Location") == 0) {
                configs->mode = NIGHTCOLOR_MODE_LOCATION;
            } else if (strcmp(keyinfo->value, "Times") == 0) {
                configs->mode = NIGHTCOLOR_MODE_TIMINGS;
            } else if (strcmp(keyinfo->value, "Constant") == 0) {
                configs->mode = NIGHTCOLOR_MODE_CONSTANT;
            } else {
                configs->mode = NIGHTCOLOR_MODE_AUTOMATIC;
            }
        } else if (strcmp(keyinfo->key, "Active") == 0) {
            if (strcmp(keyinfo->value, "true") == 0) {
                configs->active = true;
            } else {
                configs->active = false;
            }
        } else if (strcmp(keyinfo->key, "DayTemperature") == 0) {
            configs->day_colortempe = atoi(keyinfo->value);
        } else if (strcmp(keyinfo->key, "NightTemperature") == 0) {
            configs->night_colortempe = atoi(keyinfo->value);
        } else if (strcmp(keyinfo->key, "LongitudeAuto") == 0) {
            configs->longitude_auto = atof(keyinfo->value);
        } else if (strcmp(keyinfo->key, "LatitudeAuto") == 0) {
            configs->latitude_auto = atof(keyinfo->value);
        } else if (strcmp(keyinfo->key, "LongitudeFixed") == 0) {
            configs->longitude_fixed = atof(keyinfo->value);
        } else if (strcmp(keyinfo->key, "LatitudeFixed") == 0) {
            configs->latitude_fixed = atof(keyinfo->value);
        } else if (strcmp(keyinfo->key, "MorningBeginFixed") == 0) {
            configs->morning_begin_fixed = atoi(keyinfo->value);
        } else if (strcmp(keyinfo->key, "EveningBeginFixed") == 0) {
            configs->evening_begin_fixed = atoi(keyinfo->value);
        } else if (strcmp(keyinfo->key, "TransitionTime") == 0) {
            configs->transition_time = atoi(keyinfo->value);
        }

        free(keyinfo->key);
        free(keyinfo->value);
        wl_list_remove(&keyinfo->link);
        free(keyinfo);
    }

    return true;
}

static void send_change_property(enum change_property prop)
{
    const char *path = "/ColorCorrect";
    const char *interface = "org.freedesktop.DBus.properites";
    const char *member = "PropertiesChanged";

    sd_bus *bus = sd_bus_slot_get_bus(manager->config->slot);

    switch (prop) {
    case PROP_INHIBIT:
        sd_bus_emit_signal(bus, path, interface, member, "sa{sv}as", "org.kde.kwin.ColorCorrect", 1,
                           "inhibited", "b", manager->inhibit_refer_count != 0, 1, "");
        break;
    case PROP_ENABLE:
        sd_bus_emit_signal(bus, path, interface, member, "sa{sv}as", "org.kde.kwin.ColorCorrect", 1,
                           "enable", "b", manager->configs.active, 1, "");
        break;
    case PROP_RUNNING:
        sd_bus_emit_signal(bus, path, interface, member, "sa{sv}as", "org.kde.kwin.ColorCorrect", 1,
                           "running", "b", manager->running, 1, "");
        break;
    case PROP_MODE:
        sd_bus_emit_signal(bus, path, interface, member, "sa{sv}as", "org.kde.kwin.ColorCorrect", 1,
                           "mode", "u", manager->configs.mode, 1, "");
        break;
    case PROP_TARGET_COLORTEMPE:
        sd_bus_emit_signal(bus, path, interface, member, "sa{sv}as", "org.kde.kwin.ColorCorrect", 1,
                           "targetTemperature", "u", manager->target_colortempe, 1, "");
        break;
    case PROP_CURRENT_COLORTEMPE:
        sd_bus_emit_signal(bus, path, interface, member, "sa{sv}as", "org.kde.kwin.ColorCorrect", 1,
                           "currentTemperature", "u", manager->current_colortempe, 1, "");
        break;
    case PROP_PREV_TRANS_TIMING:;
        uint32_t prev_duration = manager->prev_dtime.end - manager->prev_dtime.begin;
        sd_bus_emit_signal(bus, path, interface, member, "sa{sv}as", "org.kde.kwin.ColorCorrect", 2,
                           "previousTransitionDateTime", "t", manager->prev_dtime.begin,
                           "previousTransitionDuration", "u", prev_duration, 1, "");
        break;
    case PROP_SCHED_TRANS_TIMING:;
        uint32_t sched_duration = manager->next_dtime.end - manager->next_dtime.begin;
        sd_bus_emit_signal(bus, path, interface, member, "sa{sv}as", "org.kde.kwin.ColorCorrect", 2,
                           "scheduledTransitionDateTime", "t", manager->next_dtime.begin,
                           "scheduledTransitionDuration", "u", sched_duration, 1, "");
        break;
    default:
        return;
    }
}

/* Unix epoch from Julian day */
static double epoch_from_jd(double jd)
{
    return 86400.0 * (jd - 2440587.5);
}

/* Julian day from unix epoch */
static double jd_from_epoch(double t)
{
    return (t / 86400.0) + 2440587.5;
}

/* Julian centuries since J2000.0 from Julian day */
static double jcent_from_jd(double jd)
{
    return (jd - 2451545.0) / 36525.0;
}

/* Julian day from Julian centuries since J2000.0 */
static double jd_from_jcent(double t)
{
    return 36525.0 * t + 2451545.0;
}

/* Geometric mean longitude of the sun.
 * t: Julian centuries since J2000.0
 * Return: Geometric mean logitude in radians.
 */
static double sun_geom_mean_lon(double t)
{
    /* FIXME returned value should always be positive */
    return RAD(fmod(280.46646 + t * (36000.76983 + t * 0.0003032), 360));
}

/* Geometric mean anomaly of the sun.
 * t: Julian centuries since J2000.0
 * Return: Geometric mean anomaly in radians.
 */
static double sun_geom_mean_anomaly(double t)
{
    return RAD(357.52911 + t * (35999.05029 - t * 0.0001537));
}

/* Eccentricity of earth orbit.
 * t: Julian centuries since J2000.0
 * Return: Eccentricity (unitless).
 */
static double earth_orbit_eccentricity(double t)
{
    return 0.016708634 - t * (0.000042037 + t * 0.0000001267);
}

/* Equation of center of the sun.
 * t: Julian centuries since J2000.0
 * Return: Center(?) in radians
 */
static double sun_equation_of_center(double t)
{
    /* Use the first three terms of the equation. */
    double m = sun_geom_mean_anomaly(t);
    double c = sin(m) * (1.914602 - t * (0.004817 + 0.000014 * t)) +
               sin(2 * m) * (0.019993 - 0.000101 * t) + sin(3 * m) * 0.000289;
    return RAD(c);
}

/* True longitude of the sun.
 * t: Julian centuries since J2000.0
 * Return: True longitude in radians
 */
static double sun_true_lon(double t)
{
    double l_0 = sun_geom_mean_lon(t);
    double c = sun_equation_of_center(t);
    return l_0 + c;
}

/* Apparent longitude of the sun. (Right ascension).
 * t: Julian centuries since J2000.0
 * Return: Apparent longitude in radians
 */
static double sun_apparent_lon(double t)
{
    double o = sun_true_lon(t);
    return RAD(DEG(o) - 0.00569 - 0.00478 * sin(RAD(125.04 - 1934.136 * t)));
}

/* Mean obliquity of the ecliptic
 * t: Julian centuries since J2000.0
 * Return: Mean obliquity in radians
 */
static double mean_ecliptic_obliquity(double t)
{
    double sec = 21.448 - t * (46.815 + t * (0.00059 - t * 0.001813));
    return RAD(23.0 + (26.0 + (sec / 60.0)) / 60.0);
}

/* Corrected obliquity of the ecliptic.
 * t: Julian centuries since J2000.0
 * Return: Currected obliquity in radians
 */
static double obliquity_corr(double t)
{
    double e_0 = mean_ecliptic_obliquity(t);
    double omega = 125.04 - t * 1934.136;
    return RAD(DEG(e_0) + 0.00256 * cos(RAD(omega)));
}

/* Declination of the sun.
 * t: Julian centuries since J2000.0
 * Return: Declination in radians
 */
static double solar_declination(double t)
{
    double e = obliquity_corr(t);
    double lambda = sun_apparent_lon(t);
    return asin(sin(e) * sin(lambda));
}

/* Difference between true solar time and mean solar time.
 * t: Julian centuries since J2000.0
 * Return: Difference in minutes
 */
static double equation_of_time(double t)
{
    double epsilon = obliquity_corr(t);
    double l_0 = sun_geom_mean_lon(t);
    double e = earth_orbit_eccentricity(t);
    double m = sun_geom_mean_anomaly(t);
    double y = pow(tan(epsilon / 2.0), 2.0);

    double eq_time = y * sin(2 * l_0) - 2 * e * sin(m) + 4 * e * y * sin(m) * cos(2 * l_0) -
                     0.5 * y * y * sin(4 * l_0) - 1.25 * e * e * sin(2 * m);
    return 4 * DEG(eq_time);
}

/* Hour angle at the location for the given angular elevation.
 * lat: Latitude of location in degrees
 * decl: Declination in radians
 * elev: Angular elevation angle in radians
 * Return: Hour angle in radians
 */
static double hour_angle_from_elevation(double lat, double decl, double elev)
{
    double omega =
        acos((cos(fabs(elev)) - sin(RAD(lat)) * sin(decl)) / (cos(RAD(lat)) * cos(decl)));
    return copysign(omega, -elev);
}

/* Time of apparent solar noon of location on earth.
 * t: Julian centuries since J2000.0
 * lon: Longitude of location in degrees
 * Return: Time difference from mean solar midnigth in minutes
 */
static double time_of_solar_noon(double t, double lon)
{
    /* First pass uses approximate solar noon to calculate equation of time. */
    double t_noon = jcent_from_jd(jd_from_jcent(t) - lon / 360.0);
    double eq_time = equation_of_time(t_noon);
    double sol_noon = 720 - 4 * lon - eq_time;

    /* Recalculate using new solar noon. */
    t_noon = jcent_from_jd(jd_from_jcent(t) - 0.5 + sol_noon / 1440.0);
    eq_time = equation_of_time(t_noon);
    sol_noon = 720 - 4 * lon - eq_time;

    /* No need to do more iterations */
    return sol_noon;
}

/* Time of given apparent solar angular elevation of location on earth.
 * t: Julian centuries since J2000.0
 * t_noon: Apparent solar noon in Julian centuries since J2000.0
 * lat: Latitude of location in degrees
 * lon: Longtitude of location in degrees
 * elev: Solar angular elevation in radians
 * Return: Time difference from mean solar midnight in minutes
 */
static double time_of_solar_elevation(double t, double t_noon, double lat, double lon, double elev)
{
    /* First pass uses approximate sunrise to calculate equation of time. */
    double eq_time = equation_of_time(t_noon);
    double sol_decl = solar_declination(t_noon);
    double ha = hour_angle_from_elevation(lat, sol_decl, elev);
    double sol_offset = 720 - 4 * (lon + DEG(ha)) - eq_time;

    /* Recalculate using new sunrise. */
    double t_rise = jcent_from_jd(jd_from_jcent(t) + sol_offset / 1440.0);
    eq_time = equation_of_time(t_rise);
    sol_decl = solar_declination(t_rise);
    ha = hour_angle_from_elevation(lat, sol_decl, elev);
    sol_offset = 720 - 4 * (lon + DEG(ha)) - eq_time;

    /* No need to do more iterations */
    return sol_offset;
}

static void utc_to_localtime_show(const char *title, struct date_time dtime)
{
    struct tm *tm_begin = localtime(&dtime.begin);

    kywc_log(KYWC_DEBUG, "%s-begin: %d-%d-%d %d:%d:%d", title, tm_begin->tm_year + 1900,
             tm_begin->tm_mon + 1, tm_begin->tm_mday, tm_begin->tm_hour, tm_begin->tm_min,
             tm_begin->tm_sec);

    struct tm *tm_end = localtime(&dtime.end);
    kywc_log(KYWC_DEBUG, "%s-end: %d-%d-%d %d:%d:%d", title, tm_end->tm_year + 1900,
             tm_end->tm_mon + 1, tm_end->tm_mday, tm_end->tm_hour, tm_end->tm_min, tm_end->tm_sec);
}

/* Calculate Sunrise and Sunset based on time and latitude and longitude */
static struct date_time get_sun_timings(time_t time_now, double lat, double lon, bool morning)
{
    /* Calculate Julian day */
    double jd = jd_from_epoch(time_now);

    /* Calculate Julian day number */
    double jdn = round(jd);
    double t = jcent_from_jd(jdn);

    /* Calculate apparent solar noon */
    double sol_noon = time_of_solar_noon(t, lon);
    double j_noon = jdn - 0.5 + sol_noon / 1440.0;
    double t_noon = jcent_from_jd(j_noon);

    /* angle of civil_drawn or sunset civil_dusk high */
    double angle = morning ? SOLOR_TIME_CIVIL_DAWN : SOLOR_TIME_SUNSET_CIVIL_HIGH;
    double offset = time_of_solar_elevation(t, t_noon, lat, lon, angle);
    double time_begin = epoch_from_jd(jdn - 0.5 + offset / 1440.0);
    /* sunrise civil_drawn high or sunset high */
    angle = morning ? SOLOR_TIME_SUNRISE_CIVIL_HIGH : SOLOR_TIME_CIVIL_DUSK;
    offset = time_of_solar_elevation(t, t_noon, lat, lon, angle);
    double time_end = epoch_from_jd(jdn - 0.5 + offset / 1440.0);

    struct date_time sun_time = { time_begin, time_end };
    utc_to_localtime_show("sum_timing", sun_time);
    return sun_time;
}

static void update_transition_timings(bool force)
{
    struct date_time old_prev_dtime = manager->prev_dtime;
    struct date_time old_next_dtime = manager->next_dtime;

    time_t now = time(NULL);

    if (manager->configs.mode == NIGHTCOLOR_MODE_CONSTANT) {
        struct date_time time = { 0, 0 };
        manager->prev_dtime = time;
        manager->next_dtime = time;
    } else if (manager->configs.mode == NIGHTCOLOR_MODE_TIMINGS) {
        struct tm *tm_now = localtime(&now);

        uint32_t morning_begin = manager->configs.morning_begin_fixed;
        tm_now->tm_hour = morning_begin / 100;
        tm_now->tm_min = morning_begin % 100;
        tm_now->tm_sec = 0;
        time_t tmorning = mktime(tm_now);
        time_t next_mor_b = tmorning <= now ? tmorning + 86400 : tmorning;
        time_t next_mor_e = next_mor_b + manager->configs.transition_time * 60;

        uint32_t evening_begin = manager->configs.evening_begin_fixed;
        tm_now->tm_hour = evening_begin / 100;
        tm_now->tm_min = evening_begin % 100;
        tm_now->tm_sec = 0;
        time_t tevening = mktime(tm_now);
        time_t next_eve_b = tevening <= now ? tevening + 86400 : tevening;
        time_t next_eve_e = next_eve_b + manager->configs.transition_time * 60;

        if (next_eve_b < next_mor_b) {
            manager->daylight = true;
            struct date_time next_time = { next_eve_b, next_eve_e };
            struct date_time prev_time = { next_mor_b - 86400, next_mor_e - 86400 };
            manager->next_dtime = next_time;
            manager->prev_dtime = prev_time;
        } else {
            manager->daylight = false;
            struct date_time next_time = { next_mor_b, next_mor_e };
            struct date_time prev_time = { next_eve_b - 86400, next_eve_e - 86400 };
            manager->next_dtime = next_time;
            manager->prev_dtime = prev_time;
        }
    } else { /* Automatic or location */
        double lat, lng;
        if (manager->configs.mode == NIGHTCOLOR_MODE_AUTOMATIC) {
            lat = manager->configs.latitude_auto;
            lng = manager->configs.longitude_auto;
        } else {
            lat = manager->configs.latitude_fixed;
            lng = manager->configs.latitude_fixed;
        }
        if (!force) {
            /* first try by only switching the timings */
            if (manager->prev_dtime.begin == manager->next_dtime.begin) {
                /* next is evening */
                manager->daylight = true;
                manager->prev_dtime = manager->next_dtime;
                manager->next_dtime = get_sun_timings(now, lat, lng, false);
            } else {
                /* next is moring */
                manager->daylight = false;
                manager->prev_dtime = manager->next_dtime;
                manager->next_dtime = get_sun_timings(now, lat, lng, true);
            }
        }

        if (force || !(manager->prev_dtime.begin <= now && now < manager->next_dtime.begin &&
                       manager->next_dtime.begin - manager->next_dtime.begin < 86400 * 23. / 24)) {
            struct date_time dt_morning = get_sun_timings(now, lat, lng, true);
            if (now < dt_morning.begin) {
                manager->daylight = false;
                manager->prev_dtime = get_sun_timings(now - 86400, lat, lng, false);
                manager->next_dtime = dt_morning;
            } else {
                struct date_time dt_evening = get_sun_timings(now, lat, lng, false);
                if (now < dt_evening.begin) {
                    manager->daylight = true;
                    manager->prev_dtime = dt_morning;
                    manager->next_dtime = dt_evening;
                } else {
                    manager->daylight = false;
                    manager->prev_dtime = dt_evening;
                    manager->next_dtime = get_sun_timings(now + 86400, lat, lng, true);
                }
            }
        }
    }

    utc_to_localtime_show("prev_datetime", manager->prev_dtime);
    utc_to_localtime_show("next_dateime", manager->next_dtime);

    if (old_prev_dtime.begin != manager->prev_dtime.begin ||
        old_prev_dtime.end != manager->prev_dtime.end) {
        send_change_property(PROP_PREV_TRANS_TIMING);
    }
    if (old_next_dtime.begin != manager->next_dtime.begin ||
        old_next_dtime.end != manager->prev_dtime.end) {
        send_change_property(PROP_SCHED_TRANS_TIMING);
    }
}

static void update_target_color_temperature(void)
{
    int32_t target_colortempe =
        manager->configs.mode != NIGHTCOLOR_MODE_CONSTANT && manager->daylight
            ? manager->configs.day_colortempe
            : manager->configs.night_colortempe;

    if (manager->target_colortempe != target_colortempe) {
        manager->target_colortempe = target_colortempe;
        send_change_property(PROP_TARGET_COLORTEMPE);
    }

    kywc_log(KYWC_DEBUG, "nightcolor target colortemperature :%d", manager->target_colortempe);
}

static void update_running(bool running)
{
    if (manager->running == running) {
        return;
    }

    manager->running = running;
    send_change_property(PROP_RUNNING);
}

static int handle_slow_update_start_timer(void *data);

static int32_t caculate_target_color_temperature(int target1, int target2)
{
    time_t time_now = time(NULL);

    if (time_now <= manager->prev_dtime.end) {
        int dt = manager->prev_dtime.end - time_now;
        int long_time = manager->prev_dtime.end - manager->prev_dtime.begin;
        double res_quota = (double)dt / long_time;
        double ret = (int)((1. - res_quota) * (double)target2 + res_quota * (double)target1);
        /* remove single digits */
        ret = ((int32_t)(0.1 * ret)) * 10;
        return (int32_t)ret;
    }

    return target2;
}

static int get_current_target_colortemperature(void)
{
    if (!manager->running) {
        return 6500;
    }

    if (manager->configs.mode == NIGHTCOLOR_MODE_CONSTANT) {
        return manager->target_colortempe;
    }

    if (manager->daylight) {
        return caculate_target_color_temperature(manager->configs.night_colortempe,
                                                 manager->configs.day_colortempe);
    }

    return caculate_target_color_temperature(manager->configs.day_colortempe,
                                             manager->configs.night_colortempe);
}

static void colortemperature_commit(int colortempe)
{
    struct kde_output *output;
    wl_list_for_each(output, &manager->outputs, link) {
        struct kywc_output *kywc_output = output->kywc_output;
        if (kywc_output->prop.gamma_size <= 1) {
            continue;
        }
        if (!kywc_output->state.enabled || kywc_output->state.color_temp == colortempe) {
            return;
        }

        struct kywc_output_state state = kywc_output->state;
        state.color_temp = colortempe;
        kywc_output_set_state(kywc_output, &state);
    }

    if (manager->current_colortempe != colortempe) {
        send_change_property(PROP_CURRENT_COLORTEMPE);
    }
    manager->current_colortempe = colortempe;
}

static void handle_colortemperature(bool force)
{
    /* cancel timer */
    wl_event_source_timer_update(manager->adjust_timer, 0);
    wl_event_source_timer_update(manager->slow_update_start_timer, 0);
    manager->slow_adjusting = false;
    manager->slow_update_starting = false;

    update_running(manager->configs.active && manager->inhibit_refer_count == 0);

    update_transition_timings(force);
    update_target_color_temperature();

    int target_colortempe = get_current_target_colortemperature();

    if (force) {
        colortemperature_commit(target_colortempe);
    }

    int delta_tempe = abs(target_colortempe - manager->current_colortempe);
    if (delta_tempe > COLORTEMPE_STEP) {
        int interval = QUICK_UPDATE_DURATION / (delta_tempe / COLORTEMPE_STEP);
        manager->adjust_timeout = interval;
        wl_event_source_timer_update(manager->adjust_timer, manager->adjust_timeout);
    } else {
        handle_slow_update_start_timer(NULL);
    }
}

static bool check_location_is_valid(double lat, double lng)
{
    return -90.0 <= lat && lat <= 90.0 && -180.0 <= lng && lng < 180.0;
}

static void nightcolor_update_auto_location(double latitude, double longitude)
{
    if (!check_location_is_valid(latitude, longitude)) {
        return;
    }
    /* we tolerate small deviations with minimal impact on sun timings */
    if (fabs(manager->configs.latitude_auto - latitude) < 2.0 &&
        fabs(manager->configs.longitude_auto - longitude) < 1.0) {
        return;
    }

    /* TODO: save configs */
    manager->configs.latitude_auto = latitude;
    manager->configs.longitude_auto = longitude;

    handle_colortemperature(false);
}

static bool is_available(void)
{
    struct kde_output *output;
    wl_list_for_each(output, &manager->outputs, link) {
        if (output->kywc_output->prop.gamma_size <= 1) {
            return false;
        }
    }
    return true;
}

static int inhibit(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    struct kde_nightcolor_manager *manager = userdata;
    manager->inhibit_refer_count++;
    if (manager->inhibit_refer_count) {
        handle_colortemperature(false);
        send_change_property(PROP_INHIBIT);
    }
    return sd_bus_reply_method_return(msg, "u", manager->inhibit_refer_count);
}

static int uninhibit(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    struct kde_nightcolor_manager *manager = userdata;
    uint32_t count;
    CK(sd_bus_message_read(msg, "u", &count));
    manager->inhibit_refer_count = count;
    if (!manager->inhibit_refer_count) {
        handle_colortemperature(false);
        send_change_property(PROP_INHIBIT);
    }

    return sd_bus_reply_method_return(msg, NULL);
}

static int auto_location_update(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    uint32_t latitude, longitude;
    CK(sd_bus_message_read(msg, "uu", &latitude, &longitude));
    nightcolor_update_auto_location(latitude, longitude);

    return sd_bus_reply_method_return(msg, NULL);
}

static int available(sd_bus *bus, const char *path, const char *interface, const char *property,
                     sd_bus_message *reply, void *userdata, sd_bus_error *ret_error)
{
    bool available = is_available();
    return sd_bus_message_append_basic(reply, 'b', &available);
}

static int enabled(sd_bus *bus, const char *path, const char *interface, const char *property,
                   sd_bus_message *reply, void *userdata, sd_bus_error *ret_error)
{
    struct kde_nightcolor_manager *manager = userdata;
    return sd_bus_message_append_basic(reply, 'b', &manager->configs.active);
}

static int inhibited(sd_bus *bus, const char *path, const char *interface, const char *property,
                     sd_bus_message *reply, void *userdata, sd_bus_error *ret_error)
{
    struct kde_nightcolor_manager *manager = userdata;
    bool inhibited = manager->inhibit_refer_count != 0;
    return sd_bus_message_append_basic(reply, 'b', &inhibited);
}

static int running(sd_bus *bus, const char *path, const char *interface, const char *property,
                   sd_bus_message *reply, void *userdata, sd_bus_error *ret_error)
{
    struct kde_nightcolor_manager *manager = userdata;
    return sd_bus_message_append_basic(reply, 'b', &manager->running);
}

static int mode(sd_bus *bus, const char *path, const char *interface, const char *property,
                sd_bus_message *reply, void *userdata, sd_bus_error *ret_error)
{
    struct kde_nightcolor_manager *manager = userdata;
    return sd_bus_message_append_basic(reply, 'u', &manager->configs.mode);
}

static int current_temperature(sd_bus *bus, const char *path, const char *interface,
                               const char *property, sd_bus_message *reply, void *userdata,
                               sd_bus_error *ret_error)
{
    struct kde_nightcolor_manager *manager = userdata;
    return sd_bus_message_append_basic(reply, 'u', &manager->current_colortempe);
}

static int target_temperature(sd_bus *bus, const char *path, const char *interface,
                              const char *property, sd_bus_message *reply, void *userdata,
                              sd_bus_error *ret_error)
{
    struct kde_nightcolor_manager *manager = userdata;
    return sd_bus_message_append_basic(reply, 'u', &manager->target_colortempe);
}

static int previous_transition_datetime(sd_bus *bus, const char *path, const char *interface,
                                        const char *property, sd_bus_message *reply, void *userdata,
                                        sd_bus_error *ret_error)
{
    struct kde_nightcolor_manager *manager = userdata;
    return sd_bus_message_append_basic(reply, 't', &manager->prev_dtime.begin);
}

static int previous_transition_duration(sd_bus *bus, const char *path, const char *interface,
                                        const char *property, sd_bus_message *reply, void *userdata,
                                        sd_bus_error *ret_error)
{
    struct kde_nightcolor_manager *manager = userdata;
    uint32_t prev_duration = manager->prev_dtime.end - manager->prev_dtime.begin;
    return sd_bus_message_append_basic(reply, 'u', &prev_duration);
}

static int scheduled_transition_dateTime(sd_bus *bus, const char *path, const char *interface,
                                         const char *property, sd_bus_message *reply,
                                         void *userdata, sd_bus_error *ret_error)
{
    struct kde_nightcolor_manager *manager = userdata;
    return sd_bus_message_append_basic(reply, 'u', &manager->next_dtime.begin);
}

static int scheduled_transition_duration(sd_bus *bus, const char *path, const char *interface,
                                         const char *property, sd_bus_message *reply,
                                         void *userdata, sd_bus_error *ret_error)
{
    struct kde_nightcolor_manager *manager = userdata;
    uint32_t sched_duration = manager->next_dtime.end - manager->next_dtime.begin;
    return sd_bus_message_append_basic(reply, 'u', &sched_duration);
}

static const sd_bus_vtable nightcolor_vtable[] = {
    SD_BUS_VTABLE_START(0),
    KDE_METHOD("inhibit", "", "u", inhibit),
    KDE_METHOD("nightColorAutoLocationUpdate", "dd", "", auto_location_update),
    KDE_METHOD("uninhibit", "u", "", uninhibit),

    KDE_PROPERTY("available", "b", available),
    KDE_PROPERTY("enabled", "b", enabled),
    KDE_PROPERTY("inhibited", "b", inhibited),
    KDE_PROPERTY("running", "b", running),
    KDE_PROPERTY("mode", "u", mode),
    KDE_PROPERTY("currentTemperature", "u", current_temperature),
    KDE_PROPERTY("targetTemperature", "u", target_temperature),
    KDE_PROPERTY("previousTransitionDateTime", "t", previous_transition_datetime),
    KDE_PROPERTY("previousTransitionDuration", "u", previous_transition_duration),
    KDE_PROPERTY("scheduledTransitionDateTime", "t", scheduled_transition_dateTime),
    KDE_PROPERTY("scheduledTransitionDuration", "u", scheduled_transition_duration),
    SD_BUS_VTABLE_END,
};

static int handle_colortempe_adjust_timer(void *data)
{
    int next_colortempe, target_colortempe;

    if (manager->slow_adjusting) {
        target_colortempe =
            manager->daylight ? manager->configs.day_colortempe : manager->configs.night_colortempe;
    } else {
        target_colortempe = get_current_target_colortemperature();
    }

    if (manager->current_colortempe < target_colortempe) {
        next_colortempe = MIN(manager->current_colortempe + COLORTEMPE_STEP, target_colortempe);
    } else {
        next_colortempe = MAX(manager->current_colortempe - COLORTEMPE_STEP, target_colortempe);
    }

    kywc_log(KYWC_DEBUG, "nexttempe :%d,targettempe :%d", next_colortempe, target_colortempe);

    colortemperature_commit(next_colortempe);

    if (next_colortempe != target_colortempe) {
        wl_event_source_timer_update(manager->adjust_timer, manager->adjust_timeout);
    } else if (!manager->slow_update_starting) {
        handle_slow_update_start_timer(NULL);
    }

    return 0;
}

static int handle_slow_update_start_timer(void *data)
{
    if (!manager->running || manager->configs.mode == NIGHTCOLOR_MODE_CONSTANT ||
        (!data && manager->slow_update_starting)) {
        return 0;
    }

    time_t now = time(NULL);
    update_transition_timings(false);
    update_target_color_temperature();

    int time_out = (manager->next_dtime.begin - now) * 1000;
    if (time_out <= 0) {
        manager->slow_update_starting = false;
        kywc_log(KYWC_WARN, "error in time calculation,deactivating nightcolor");
        return 0;
    }

    manager->slow_update_starting = true;
    wl_event_source_timer_update(manager->slow_update_start_timer, time_out);
    kywc_log(KYWC_INFO, "nightcolor slow update timer start:%d", time_out);

    // We've reached the target color temperature or the transition time is zero.
    if (manager->prev_dtime.begin == manager->prev_dtime.end ||
        manager->current_colortempe == manager->target_colortempe) {
        colortemperature_commit(manager->target_colortempe);
        return 0;
    }

    if (manager->prev_dtime.begin <= now && now <= manager->prev_dtime.end) {
        int target_colortempe =
            manager->daylight ? manager->configs.day_colortempe : manager->configs.night_colortempe;
        int avail_time = (manager->prev_dtime.end - now) * 1000;
        int interval =
            avail_time * COLORTEMPE_STEP / abs(target_colortempe - manager->current_colortempe);

        // calculate interval such as temperature is changed by TEMPERATURE_STEP K per timer timeout
        manager->adjust_timeout = interval == 0 ? 1 : interval;
        manager->slow_adjusting = true;
        wl_event_source_timer_update(manager->adjust_timer, manager->adjust_timeout);
        kywc_log(KYWC_INFO, "nightcolor slow adjust timer start :%d", manager->adjust_timeout);
    }

    return 0;
}

static void update_active(bool enable)
{
    if (manager->configs.active == enable) {
        return;
    }

    manager->configs.active = enable;
    send_change_property(PROP_ENABLE);
}

static void update_mode(enum nightcolor_mode mode)
{
    if (manager->configs.mode == mode) {
        return;
    }

    manager->configs.mode = mode;
    send_change_property(PROP_MODE);
}

static void load_manager_configs(void)
{
    struct nigcolor_configs configs;
    if (read_nightcolor_configs(&configs)) {
        // automatic
        if (!(check_location_is_valid(configs.latitude_auto, configs.longitude_auto))) {
            configs.latitude_auto = 0.0;
            configs.longitude_auto = 0.0;
        }
        // fixed loaction
        if (!(check_location_is_valid(configs.latitude_fixed, configs.longitude_fixed))) {
            configs.latitude_fixed = 0.0;
            configs.longitude_fixed = 0.0;
        }
        // fixed timings
        uint32_t mor_begin =
            configs.morning_begin_fixed / 100 * 3600 + configs.morning_begin_fixed % 100 * 60;
        uint32_t eve_begin =
            configs.evening_begin_fixed / 100 * 3600 + configs.evening_begin_fixed % 100 * 60;

        int daylight_tm = eve_begin > mor_begin ? eve_begin - mor_begin : mor_begin - eve_begin;
        int night_tm = 86400 - daylight_tm;
        int diff_tm = MIN(daylight_tm, night_tm);
        int trs_tm = configs.transition_time;

        if (trs_tm < 0 || diff_tm <= trs_tm * 60) {
            // transition time too long - use defaults
            mor_begin = 6 * 100;  // 6:00
            eve_begin = 18 * 100; // 18:00
            trs_tm = 30;
            configs.morning_begin_fixed = mor_begin;
            configs.evening_begin_fixed = eve_begin;
        }

        configs.transition_time = MAX(trs_tm, 1);
        configs.night_colortempe = CLAMP(configs.night_colortempe, 1000, 6500);
        configs.day_colortempe = CLAMP(configs.day_colortempe, 1000, 6500);

        kywc_log(KYWC_INFO,
                 "configs mode:%d, active:%d, nighttempe:%d, daytempe%d, morn_begin:%d, "
                 "even_begin:%d, lat:%f, lng:%f",
                 configs.mode, configs.active, configs.night_colortempe, configs.day_colortempe,
                 configs.morning_begin_fixed, configs.evening_begin_fixed, configs.latitude_auto,
                 configs.longitude_auto);

        update_active(configs.active);
        update_mode(configs.mode);

        manager->configs = configs;
    }
}

static int config_changed(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    CK(sd_bus_message_enter_container(msg, SD_BUS_TYPE_ARRAY, "{saay}"));

    while (sd_bus_message_enter_container(msg, SD_BUS_TYPE_DICT_ENTRY, "saay") > 0) {
        const char *config = "null";
        CK(sd_bus_message_read(msg, "s", &config));
        if (strcmp(config, "NightColor")) {
            continue;
        }
        while (sd_bus_message_enter_container(msg, SD_BUS_TYPE_ARRAY, "ay") > 0) {
            uint8_t *ptr;
            size_t size;
            if (sd_bus_message_read_array(msg, 'y', (void *)&ptr, &size) <= 0) {
                continue;
            }
            char *mode = calloc(1, size + 1);
            memcpy(mode, ptr, size);
            mode[size] = '\0';

            if (strcmp(mode, "NightColor.Mode") == 0) {
                load_manager_configs();
                handle_colortemperature(!manager->initial);
                manager->initial = !manager->initial ? true : manager->initial;
            }
            free(mode);
        }
    }

    return 0;
}

static void nightcolor_manager_init(struct kde_nightcolor_manager *manager)
{
    manager->running = false;
    manager->daylight = true;
    manager->inhibit_refer_count = 0;
    manager->current_colortempe = 6500;
    manager->target_colortempe = 6500;

    configs_init(&manager->configs);
    load_manager_configs();
    handle_colortemperature(true);
}

static void kde_output_destroy(struct kde_output *output)
{
    wl_list_remove(&output->link);
    wl_list_remove(&output->on.link);
    wl_list_remove(&output->destroy.link);

    free(output);
}

static void handle_output_destroy(struct wl_listener *listener, void *data)
{
    struct kde_output *output = wl_container_of(listener, output, destroy);
    kde_output_destroy(output);
}

static void handle_output_on(struct wl_listener *listener, void *data)
{
    struct kde_output *output = wl_container_of(listener, output, on);
    struct kywc_output *kywc_output = output->kywc_output;

    if (kywc_output->prop.gamma_size <= 1) {
        return;
    }
    struct kywc_output_state state = kywc_output->state;
    state.color_temp = manager->current_colortempe;
    kywc_output_set_state(kywc_output, &state);
}

static void handle_new_output(struct wl_listener *listener, void *data)
{
    struct kywc_output *kywc_output = data;
    if (kywc_output->prop.is_virtual) {
        return;
    }

    struct kde_output *output = calloc(1, sizeof(struct kde_output));
    if (!output) {
        return;
    }

    output->kywc_output = kywc_output;

    struct kde_nightcolor_manager *manager = wl_container_of(listener, manager, new_output);
    wl_list_insert(&manager->outputs, &output->link);

    output->on.notify = handle_output_on;
    wl_signal_add(&kywc_output->events.on, &output->on);

    output->destroy.notify = handle_output_destroy;
    wl_signal_add(&kywc_output->events.destroy, &output->destroy);

    handle_colortemperature(true);
}

static void handle_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&manager->new_output.link);
    wl_list_remove(&manager->destroy.link);

    wl_list_remove(&manager->server_suspend.link);
    wl_list_remove(&manager->server_resume.link);
    wl_list_remove(&manager->session_active.link);

    if (manager->clockskew) {
        wl_event_source_remove(manager->clockskew);
        free(manager->clockskew);
    }

    wl_event_source_remove(manager->adjust_timer);
    wl_event_source_remove(manager->slow_update_start_timer);
    free(manager->adjust_timer);
    free(manager->slow_update_start_timer);

    /* destroy kde_output devices*/
    struct kde_output *output, *output_tmp;
    wl_list_for_each_safe(output, output_tmp, &manager->outputs, link) {
        kde_output_destroy(output);
    }

    free(manager);
}

static void handle_server_suspend(struct wl_listener *listener, void *data)
{
    /* cancel timer */
    wl_event_source_timer_update(manager->adjust_timer, 0);
    wl_event_source_timer_update(manager->slow_update_start_timer, 0);

    if (manager->clockskew) {
        wl_event_source_fd_update(manager->clockskew, WL_EVENT_HANGUP);
    }
}

static void handle_server_resume(struct wl_listener *listener, void *data)
{
    if (manager->clockskew) {
        wl_event_source_fd_update(manager->clockskew, WL_EVENT_READABLE);
    }

    handle_colortemperature(true);
}

static void handle_session_active(struct wl_listener *listener, void *data)
{
    bool active = data;
    if (!active) {
        /* cancel timer */
        wl_event_source_timer_update(manager->adjust_timer, 0);
        wl_event_source_timer_update(manager->slow_update_start_timer, 0);
        wl_event_source_fd_update(manager->clockskew, WL_EVENT_HANGUP);
    } else {
        wl_event_source_fd_update(manager->clockskew, WL_EVENT_READABLE);
        handle_colortemperature(true);
    }
}

static int time_change_event(int fd, uint32_t mask, void *data)
{
    uint64_t expiration_count;
    read(fd, &expiration_count, sizeof(expiration_count));

    if (mask & WL_EVENT_ERROR || mask & WL_EVENT_HANGUP) {
        return 0;
    }

    struct kde_nightcolor_manager *manager = data;
    if (manager->configs.active) {
        kywc_log(KYWC_INFO, "nightcolor handle time change");
        handle_colortemperature(true);
    }

    return 0;
}

static int time_change_fd(void)
{
    /* We only care for the cancellation event
     * hence we set the timeout to the latest possible value.
     */
    const struct itimerspec its = {};
    /* Uses TFD_TIMER_CANCEL_ON_SET to get notifications
     * whenever CLOCK_REALTIME makes a jump relative to CLOCK_MONOTONIC.
     */
    int fd = timerfd_create(CLOCK_REALTIME, TFD_NONBLOCK | TFD_CLOEXEC);
    if (fd < 0) {
        kywc_log(KYWC_ERROR, "timerfd for clockskew create failed");
        return -1;
    }
    if (timerfd_settime(fd, TFD_TIMER_ABSTIME | TFD_TIMER_CANCEL_ON_SET, &its, NULL) < 0) {
        kywc_log(KYWC_ERROR, "timerfd for clockskew settime  failed");
        return -1;
    }

    return fd;
}

bool kde_nightcolor_manager_create(struct config_manager *config_manager)
{
    manager = calloc(1, sizeof(struct kde_nightcolor_manager));
    if (!manager) {
        return false;
    }

    manager->config = config_manager_add_config(NULL, service_bus, service_path, service_interface,
                                                nightcolor_vtable, manager);
    if (!manager->config) {
        free(manager);
        return false;
    }
    sd_bus_match_signal(config_manager->bus, NULL, NULL, "/kwinrc", "org.kde.kconfig.notify",
                        "ConfigChanged", config_changed, manager);

    struct wl_display *display = config_manager->server->display;
    struct wl_event_loop *loop = wl_display_get_event_loop(display);

    manager->adjust_timer = wl_event_loop_add_timer(loop, handle_colortempe_adjust_timer, manager);
    if (!manager->adjust_timer) {
        free(manager);
        return false;
    }
    manager->slow_update_start_timer =
        wl_event_loop_add_timer(loop, handle_slow_update_start_timer, manager);
    if (!manager->slow_update_start_timer) {
        wl_event_source_remove(manager->adjust_timer);
        free(manager);
        return false;
    }

    int fd = time_change_fd();
    if (fd > 0) {
        manager->clockskew =
            wl_event_loop_add_fd(loop, fd, WL_EVENT_READABLE, time_change_event, manager);
    }

    wl_list_init(&manager->outputs);

    manager->server_suspend.notify = handle_server_suspend;
    wl_signal_add(&config_manager->server->events.suspend, &manager->server_suspend);

    manager->server_resume.notify = handle_server_resume;
    wl_signal_add(&config_manager->server->events.resume, &manager->server_resume);

    manager->session_active.notify = handle_session_active;
    wl_signal_add(&config_manager->server->events.active, &manager->session_active);

    manager->new_output.notify = handle_new_output;
    kywc_output_add_new_listener(&manager->new_output);

    manager->destroy.notify = handle_destroy;
    wl_signal_add(&manager->config->events.destroy, &manager->destroy);

    nightcolor_manager_init(manager);

    return true;
}
