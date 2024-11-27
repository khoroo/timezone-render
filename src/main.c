#include "raylib.h"
#include "raymath.h"
#include <json-c/json.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

typedef struct {
    Vector2* points;
    int count;
} Polygon;

typedef struct {
    Polygon* polygons;
    int count;
    Rectangle bounds;
} GeoData;

typedef struct {
    char* tzid;
    Color color;
} TimezoneColor;

unsigned int getColor(int index) {
    if (index < 0 || index >= 80) {
        return 0;  // Return black for invalid indices
    }
    
    // Break the space into 4 levels per channel
    int r = (index % 4) * 85;          // 85 is roughly 255/3
    int g = ((index / 4) % 4) * 85;
    int b = ((index / 16) % 4) * 85;
    
    // If we've used up our RGB combinations and still need colors,
    // generate some grayscale values
    if (index >= 64) {
        int gray = ((index - 64) * 16);  // Spread remaining 16 values across grayscale
        return (gray << 16) | (gray << 8) | gray;
    }
    
    return (r << 16) | (g << 8) | b;
}
static int color_index = 0;

static Color next_color() {
    unsigned int rgb = getColor(color_index++ % 80);  // Wrap around after 80 colors
    
    return (Color){
        (unsigned char)((rgb >> 16) & 0xFF),  // Red
        (unsigned char)((rgb >> 8) & 0xFF),   // Green
        (unsigned char)(rgb & 0xFF),          // Blue
        255                                   // Alpha
    };
}


static void update_bounds(Rectangle* bounds, float x, float y) {
    if (bounds->width == 0 && bounds->height == 0) {
        bounds->x = x;
        bounds->y = y;
        bounds->width = x;
        bounds->height = y;
    } else {
        bounds->x = fminf(bounds->x, x);
        bounds->y = fminf(bounds->y, y);
        bounds->width = fmaxf(bounds->width, x);
        bounds->height = fmaxf(bounds->height, y);
    }
}

static void process_coordinates(json_object* coords, GeoData* data) {
    if (!coords || json_object_get_type(coords) != json_type_array) return;
    
    int point_count = json_object_array_length(coords);
    Vector2* points = malloc(point_count * sizeof(Vector2));
    
    for (int i = 0; i < point_count; i++) {
        json_object* coord = json_object_array_get_idx(coords, i);
        float x = json_object_get_double(json_object_array_get_idx(coord, 0));
        float y = json_object_get_double(json_object_array_get_idx(coord, 1));
        points[i] = (Vector2){x, y};
        update_bounds(&data->bounds, x, y);
    }
    
    data->polygons = realloc(data->polygons, (data->count + 1) * sizeof(Polygon));
    data->polygons[data->count].points = points;
    data->polygons[data->count].count = point_count;
    data->count++;
}

static void process_geometry(json_object* geometry, GeoData* data) {
    json_object* type, *coordinates;
    
    if (!json_object_object_get_ex(geometry, "type", &type) ||
        !json_object_object_get_ex(geometry, "coordinates", &coordinates)) return;
        
    const char* geom_type = json_object_get_string(type);
    
    if (strcmp(geom_type, "Polygon") == 0) {
        int ring_count = json_object_array_length(coordinates);
        for (int i = 0; i < ring_count; i++) {
            process_coordinates(json_object_array_get_idx(coordinates, i), data);
        }
    } else if (strcmp(geom_type, "MultiPolygon") == 0) {
        int poly_count = json_object_array_length(coordinates);
        for (int i = 0; i < poly_count; i++) {
            json_object* polygon = json_object_array_get_idx(coordinates, i);
            int ring_count = json_object_array_length(polygon);
            for (int j = 0; j < ring_count; j++) {
                process_coordinates(json_object_array_get_idx(polygon, j), data);
            }
        }
    }
}

static Vector2 world_to_screen(Vector2 point, Rectangle bounds, Rectangle screen) {
    float scale = fminf(
        screen.width / (bounds.width - bounds.x),
        screen.height / (bounds.height - bounds.y)
    );
    
    return (Vector2){
        (point.x - bounds.x) * scale,
        screen.height - ((point.y - bounds.y) * scale)
    };
}

static void save_color_mapping(json_object* features, const char* filename) {
    json_object* root = json_object_new_object();
    json_object* mapping = json_object_new_object();
    
    int feature_count = json_object_array_length(features);
    for (int i = 0; i < feature_count; i++) {
        json_object* feature = json_object_array_get_idx(features, i);
        json_object* properties, *tzid;
        
        if (json_object_object_get_ex(feature, "properties", &properties) &&
            json_object_object_get_ex(properties, "tzid", &tzid)) {
            
            Color c = next_color();
            char colorHex[8];
            snprintf(colorHex, sizeof(colorHex), "#%02X%02X%02X", c.r, c.g, c.b);
            json_object_object_add(mapping, json_object_get_string(tzid),
                                 json_object_new_string(colorHex));
            color_index++;
        }
    }
    
    json_object_object_add(root, "color_mapping", mapping);
    
    FILE* fp = fopen(filename, "w");
    if (fp) {
        fprintf(fp, "%s\n", json_object_to_json_string_ext(root, JSON_C_TO_STRING_PRETTY | JSON_C_TO_STRING_NOSLASHESCAPE));
        fclose(fp);
    }
    
    json_object_put(root);
}

static void floodFillPolygon(Vector2* points, int pointCount, Color color) {
    float minX = points[0].x;
    float maxX = points[0].x;
    float minY = points[0].y;
    float maxY = points[0].y;
    
    for (int i = 1; i < pointCount; i++) {
        if (points[i].x < minX) minX = points[i].x;
        if (points[i].x > maxX) maxX = points[i].x;
        if (points[i].y < minY) minY = points[i].y;
        if (points[i].y > maxY) maxY = points[i].y;
    }

    for (int y = minY; y <= maxY; y++) {
        int intersections[256];
        int intersectionCount = 0;
        
        for (int i = 0, j = pointCount - 1; i < pointCount; j = i++) {
            if ((points[i].y > y) != (points[j].y > y)) {
                float x = points[j].x + (points[i].x - points[j].x) * 
                         (y - points[j].y) / (points[i].y - points[j].y);
                if (intersectionCount < 256) {
                    intersections[intersectionCount++] = x;
                }
            }
        }
        
        for (int i = 0; i < intersectionCount - 1; i++) {
            for (int j = 0; j < intersectionCount - i - 1; j++) {
                if (intersections[j] > intersections[j + 1]) {
                    int temp = intersections[j];
                    intersections[j] = intersections[j + 1];
                    intersections[j + 1] = temp;
                }
            }
        }
        
        for (int i = 0; i < intersectionCount - 1; i += 2) {
            int startX = intersections[i];
            int endX = intersections[i + 1];
            DrawLineEx((Vector2){startX, y}, 
                      (Vector2){endX, y}, 
                      1.0f, color);
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        printf("Usage: %s <geojson-file>\n", argv[0]);
        return 1;
    }

    FILE* fp = fopen(argv[1], "r");
    if (!fp) return 1;
    
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    char* buffer = malloc(size + 1);
    fread(buffer, 1, size, fp);
    buffer[size] = '\0';
    fclose(fp);
    
    json_object* root = json_tokener_parse(buffer);
    free(buffer);
    
    GeoData data = {0};
    json_object* features;
    if (json_object_object_get_ex(root, "features", &features)) {
        save_color_mapping(features, "timezone_colors.json");
        
        int feature_count = json_object_array_length(features);
        for (int i = 0; i < feature_count; i++) {
            json_object* feature = json_object_array_get_idx(features, i);
            json_object* geometry;
            if (json_object_object_get_ex(feature, "geometry", &geometry)) {
                process_geometry(geometry, &data);
            }
        }
    }
    
    float width = data.bounds.width - data.bounds.x;
    float height = data.bounds.height - data.bounds.y;
    float aspect = width / height;
    
    int screenHeight = 600;
    int screenWidth = (int)(screenHeight * aspect);
    
    InitWindow(screenWidth, screenHeight, "GeoJSON Viewer");
    SetTargetFPS(60);
    
    Rectangle screen = {0, 0, screenWidth, screenHeight};
    
    // Create render texture to draw our image
    RenderTexture2D target = LoadRenderTexture(screenWidth, screenHeight);
    
    // Draw to render texture
    BeginTextureMode(target);
        ClearBackground(RAYWHITE);
        
        color_index = 0;
        
        for (int i = 0; i < data.count; i++) {
            Vector2* screenPoints = malloc(data.polygons[i].count * sizeof(Vector2));
            
            for (int j = 0; j < data.polygons[i].count; j++) {
                screenPoints[j] = world_to_screen(data.polygons[i].points[j], 
                                                data.bounds, screen);
            }
            
            Color fillColor = next_color();
            fillColor.a = 255; // Make it fully opaque for the final render
            floodFillPolygon(screenPoints, data.polygons[i].count, fillColor);
            
            free(screenPoints);
            color_index++;
        }
    EndTextureMode();
    
    // Save the render texture as PNG
    Image image = LoadImageFromTexture(target.texture);
    ImageFlipVertical(&image); // Flip the image as raylib renders it upside down
    ExportImage(image, "output.png");
    UnloadImage(image);
    
    // Cleanup
    UnloadRenderTexture(target);
    for (int i = 0; i < data.count; i++) {
        free(data.polygons[i].points);
    }
    free(data.polygons);
    json_object_put(root);
    CloseWindow();
    
    return 0;
}
