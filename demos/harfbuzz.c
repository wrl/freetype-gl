/* Freetype GL - A C OpenGL Freetype engine
 *
 * Distributed under the OSI-approved BSD 2-Clause License.  See accompanying
 * file `LICENSE` for more details.
 *
 * ============================================================================
 *
 * All credits go to https://github.com/lxnt/ex-sdl-freetype-harfbuzz
 *
 * ============================================================================
 */
#include <math.h>
#include <string.h>

#include <sys/stat.h>
#include <unistd.h>

#include "freetype-gl.h"
#include "mat4.h"
#include "shader.h"
#include "vertex-buffer.h"
#include "texture-font.h"
#include "screenshot-util.h"

#include <GLFW/glfw3.h>

#include <lo/lo.h>


/* google this */
#ifndef unlikely
#define unlikely
#endif


// ------------------------------------------------------- global variables ---
GLuint shader;
texture_atlas_t *atlas;
vertex_buffer_t * vbuffer;
mat4 model, view, projection;

time_t fea_mtime = 0;

lo_server osc_in;

const char *text = "Toward kerning";
const char *font_filename      = "/home/wrl/work/google/plex/IBM-Plex-Sans/sources/masters/master_ttf/IBM Plex Sans-Regular.ttf";
const char *fea_path = "/home/wrl/work/google/plex/IBM-Plex-Sans/sources/masters/IBM Plex Sans-Regular.ufo/features.min.fea";
const hb_direction_t direction = HB_DIRECTION_LTR;
const hb_script_t script       = HB_SCRIPT_LATIN;
const char *language           = "en";


void
dbg_font(texture_font_t *f)
{
	unsigned int x_ppem, y_ppem;
	int x_scale, y_scale;

	hb_font_get_ppem(f->hb_ft_font, &x_ppem, &y_ppem);
	hb_font_get_scale(f->hb_ft_font, &x_scale, &y_scale);

	printf("[%u %u] [%d %d]\n",
		x_ppem, y_ppem, x_scale, y_scale);
}

// ------------------------------------------------------------------- init ---
#define NFONTS 1

texture_font_t *fonts[NFONTS];
typedef struct { float x,y,z, u,v, r,g,b,a, shift, gamma; } vertex_t;

static void
reload_features(void)
{
	texture_font_t *f;
	struct stat sb;

	for (unsigned i = 0; i < NFONTS; i++) {
		f = fonts[i];

		if (frs_merge_features(f->frs_state, fea_path))
			continue;

		tf_recreate_hb(f);
	}

	if (!stat(fea_path, &sb))
		fea_mtime = sb.st_mtime;
}

static int
fea_has_changed(void)
{
	struct stat sb;

	if (stat(fea_path, &sb))
		return 0;

	return sb.st_mtime > fea_mtime;
}

void render(void);

static void
clobber_bits(unsigned idx, int16_t delta)
{
	texture_font_t *f;

	for (unsigned i = 0; i < NFONTS; i++) {
		f = fonts[i];

		frs_clobber_bits(f->frs_state, idx, delta);
		tf_recreate_hb(f);
	}

	render();
}

void
init(void)
{
    size_t i;
	int size = 76;

    atlas = texture_atlas_new( 512, 512, 3 );
    for ( i=0; i< NFONTS; ++i )
    {
        fonts[i] =  texture_font_new_from_file(atlas, size+(i * 4), font_filename),
        texture_font_load_glyphs(fonts[i], text, language );
    }

	reload_features();

    vbuffer = vertex_buffer_new( "vertex:3f,tex_coord:2f,"
                                "color:4f,ashift:1f,agamma:1f" );
    glGenTextures( 1, &atlas->id );
    glClearColor(1,1,1,1);
    glEnable( GL_BLEND );
    glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
    glBindTexture( GL_TEXTURE_2D, atlas->id );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
    glTexImage2D( GL_TEXTURE_2D, 0, GL_RGB, atlas->width, atlas->height,
                  0, GL_RGB, GL_UNSIGNED_BYTE, atlas->data );
    shader = shader_load("shaders/text.vert", "shaders/text.frag");
    mat4_set_identity( &projection );
    mat4_set_identity( &model );
    mat4_set_identity( &view );
}

void
render(void)
{
    size_t i, j;

	vertex_buffer_clear(vbuffer);

    /* Create a buffer for harfbuzz to use */
    hb_buffer_t *buffer = hb_buffer_create();

    for (i=0; i < NFONTS; ++i)
    {
        hb_buffer_set_language( buffer,
                                hb_language_from_string(language, strlen(language)) );
        hb_buffer_add_utf8( buffer, text, strlen(text), 0, strlen(text) );
        hb_buffer_guess_segment_properties( buffer );
        hb_shape( fonts[i]->hb_ft_font, buffer, NULL, 0 );

        unsigned int         glyph_count;
        hb_glyph_info_t     *glyph_info =
            hb_buffer_get_glyph_infos(buffer, &glyph_count);
        hb_glyph_position_t *glyph_pos =
            hb_buffer_get_glyph_positions(buffer, &glyph_count);

        texture_font_load_glyphs( fonts[i], text, language );

        float gamma = 1.0;
        float shift = 0.0;
        float x = 0;
        float y = 600 - i * (45+i) - 360;
        float width = 0.0;
        float hres = fonts[i]->hres;
        for (j = 0; j < glyph_count; ++j)
        {
            int codepoint = glyph_info[j].codepoint;
            float x_advance = glyph_pos[j].x_advance/(float)(hres*64);
            float x_offset = glyph_pos[j].x_offset/(float)(hres*64);
            texture_glyph_t *glyph = texture_font_get_glyph(fonts[i], codepoint);
            if( i < (glyph_count-1) )
                width += x_advance + x_offset;
            else
                width += glyph->offset_x + glyph->width;
        }

        x = 800 - width - 10 ;
        for (j = 0; j < glyph_count; ++j)
        {
            int codepoint = glyph_info[j].codepoint;
            // because of vhinting trick we need the extra 64 (hres)
            float x_advance = glyph_pos[j].x_advance/(float)(hres*64);
            float x_offset = glyph_pos[j].x_offset/(float)(hres*64);
            float y_advance = glyph_pos[j].y_advance/(float)(64);
            float y_offset = glyph_pos[j].y_offset/(float)(64);
            texture_glyph_t *glyph = texture_font_get_glyph(fonts[i], codepoint);

            float r = 0.0;
            float g = 0.0;
            float b = 0.0;
            float a = 1.0;
            float x0 = x + x_offset + glyph->offset_x;
            float x1 = x0 + glyph->width;
            float y0 = floor(y + y_offset + glyph->offset_y);
            float y1 = floor(y0 - glyph->height);
            float s0 = glyph->s0;
            float t0 = glyph->t0;
            float s1 = glyph->s1;
            float t1 = glyph->t1;
            vertex_t vertices[4] =  {
                {x0,y0,0, s0,t0, r,g,b,a, shift, gamma},
                {x0,y1,0, s0,t1, r,g,b,a, shift, gamma},
                {x1,y1,0, s1,t1, r,g,b,a, shift, gamma},
                {x1,y0,0, s1,t0, r,g,b,a, shift, gamma} };
            GLuint indices[6] = { 0,1,2, 0,2,3 };
            vertex_buffer_push_back( vbuffer, vertices, 4, indices, 6 );
            x += x_advance;
            y += y_advance;
        }
        /* clean up the buffer, but don't kill it just yet */
        hb_buffer_reset(buffer);
    }

    vertex_buffer_upload( vbuffer );
	hb_buffer_destroy(buffer);
}


// ---------------------------------------------------------------- display ---
int16_t frame_deltas[4] = {0, 0, 0, 0};

static int
enc_handler(const char *path, const char *types, lo_arg **argv, int argc,
		lo_message data, void *user_data)
{
	unsigned idx = argv[0]->i;
	int delta = argv[1]->i;

	frame_deltas[idx] -= delta;
}


void display( GLFWwindow* window )
{
	unsigned i;

	while (lo_server_recv_noblock(osc_in, 0) > 0);

	if (fea_has_changed()) {
		reload_features();
		render();

		for (i = 0; i < 4; i++)
			frame_deltas[i] = 0;
	} else {
		for (i = 0; i < 4; i++) {
			if (frame_deltas[i] == 0)
				continue;

			clobber_bits(i, frame_deltas[i]);
			frame_deltas[i] = 0;
		}
	}

    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

    glUseProgram( shader );
    {
        glUniform1i( glGetUniformLocation( shader, "texture" ),
                     0 );
        glUniform3f( glGetUniformLocation( shader, "pixel" ), 1/512., 1/512., 1.0f );

        glUniformMatrix4fv( glGetUniformLocation( shader, "model" ),
                            1, 0, model.data);
        glUniformMatrix4fv( glGetUniformLocation( shader, "view" ),
                            1, 0, view.data);
        glUniformMatrix4fv( glGetUniformLocation( shader, "projection" ),
                            1, 0, projection.data);
        vertex_buffer_render( vbuffer, GL_TRIANGLES );
    }

    glfwSwapBuffers( window );
}


// ---------------------------------------------------------------- reshape ---
void reshape( GLFWwindow* window, int width, int height )
{
    glViewport(0, 0, width, height);
    mat4_set_orthographic( &projection, 0, width, 0, height, -1, 1);
}


// --------------------------------------------------------------- keyboard ---
void keyboard( GLFWwindow* window, int key, int scancode, int action, int mods )
{
	if (action != GLFW_PRESS)
		return;

	switch (key) {
	case GLFW_KEY_ESCAPE:
	case GLFW_KEY_Q:
        glfwSetWindowShouldClose( window, GL_TRUE );
		break;

	case GLFW_KEY_R:
		reload_features();
		render();

		break;

	case GLFW_KEY_MINUS: clobber_bits(0, -10); break;
	case GLFW_KEY_EQUAL: clobber_bits(0, 10); break;

	case GLFW_KEY_LEFT_BRACKET:  clobber_bits(1, -10); break;
	case GLFW_KEY_RIGHT_BRACKET: clobber_bits(1, 10); break;

	default:
		break;
	}
}


// --------------------------------------------------------- error-callback ---
void error_callback( int error, const char* description )
{
    fputs( description, stderr );
}

// ------------------------------------------------------------------- main ---
int main( int argc, char **argv )
{
    GLFWwindow* window;
    char* screenshot_path = NULL;

    if (argc > 1)
    {
        if (argc == 3 && 0 == strcmp( "--screenshot", argv[1] ))
            screenshot_path = argv[2];
        else
        {
            fprintf( stderr, "Unknown or incomplete parameters given\n" );
            exit( EXIT_FAILURE );
        }
    }

    glfwSetErrorCallback( error_callback );

    if (!glfwInit( ))
    {
        exit( EXIT_FAILURE );
    }

    glfwWindowHint( GLFW_VISIBLE, GL_TRUE );
    glfwWindowHint( GLFW_RESIZABLE, GL_FALSE );

	osc_in = lo_server_new("42424", NULL);
	if (!osc_in) {
		puts("couldn't create OSC server!");
		return 1;
	}

	lo_server_add_method(osc_in, "/monome/enc/delta", "ii", enc_handler, NULL);

    window = glfwCreateWindow( 800, 600, argv[0], NULL, NULL );

    if (!window)
    {
        glfwTerminate( );
        exit( EXIT_FAILURE );
    }

    glfwMakeContextCurrent( window );
    glfwSwapInterval( 1 );

    glfwSetFramebufferSizeCallback( window, reshape );
    glfwSetWindowRefreshCallback( window, display );
    glfwSetKeyCallback( window, keyboard );

#ifndef __APPLE__
    glewExperimental = GL_TRUE;
    GLenum err = glewInit();
    if (GLEW_OK != err)
    {
        /* Problem: glewInit failed, something is seriously wrong. */
        fprintf( stderr, "Error: %s\n", glewGetErrorString(err) );
        exit( EXIT_FAILURE );
    }
    fprintf( stderr, "Using GLEW %s\n", glewGetString(GLEW_VERSION) );
#endif

    init();
	render();

    glfwShowWindow( window );
    reshape( window, 800, 600 );

    while(!glfwWindowShouldClose( window ))
    {
        display( window );
        glfwPollEvents( );

        if (screenshot_path)
        {
            screenshot( window, screenshot_path );
            glfwSetWindowShouldClose( window, 1 );
        }
    }

    glDeleteTextures( 1, &atlas->id );
    atlas->id = 0;
    texture_atlas_delete( atlas );

    glfwDestroyWindow( window );
    glfwTerminate( );

    return EXIT_SUCCESS;
}
