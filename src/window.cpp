#include <iostream>
#include <cassert>
#include <fstream>
#include <iterator>
#include <cstring>
#include <string>
#include <chrono>
#include <algorithm>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "glm/ext.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include <GL/glew.h> // Put before other OpenGL library headers
#include <GL/gl.h>
#include <GL/glx.h>  // X11-specific APIs

#include "../include/callbacks.hpp"
#include "../../liblac/include/matmath.h"

#define APP_TITLE "KingCraft"

// X11 variables
Display                *dpy;  // The target monitor/display
Window                  win;  // The application's window
Screen                 *scrn; //
int                     scrn_id;
XVisualInfo            *vi;   // Struct containing additional info about the window
XWindowAttributes       gwa;  // Get attributes struct
XEvent                  xev;  // XEvent struct for handling X events
GLXContext              glx;  // The OpenGL context for X11

using namespace std::chrono;
void CalculateFrameRate(int &fps, int &_fpsCount, steady_clock::time_point &lastTime)
{
    auto currentTime = steady_clock::now();

    const auto elapsedTime = duration_cast<nanoseconds>(currentTime - lastTime).count();
    ++_fpsCount;

    if (elapsedTime > 1000000000)
    {
        lastTime = currentTime;
        fps = _fpsCount;
        _fpsCount = 0;

        std::cout << "FPS: " << fps << std::endl; // print out fps in every second (or you can use it elsewhere)
    }
}

typedef GLXContext (*glXCreateContextAttribsARBProc)(Display*, GLXFBConfig, GLXContext, Bool, const int *);

static unsigned CompileShader(unsigned type, const std::string source)
{
   unsigned int id = glCreateShader(type);
   const char *src = source.c_str();
   glShaderSource(id, 1, &src, NULL);
   glCompileShader(id);

   int result;
   glGetShaderiv(id, GL_COMPILE_STATUS, &result); // Get shader compilation status
   if (result == GL_FALSE) // Print out reason for failure
   {
      int length;
      glGetShaderiv(id, GL_INFO_LOG_LENGTH, &length);
      char* message = (char*)alloca(length * sizeof(char));
      glGetShaderInfoLog(id, length, &length, message);
      std::cerr << "Failed to compile " << (type == GL_VERTEX_SHADER ? "vertex" : "fragment")
         << " shader: " << message << std::endl;
      glDeleteShader(id);
      return 0;
   }

   return id;
}

static unsigned CreateShader(const std::string vertexShader, const std::string fragmentShader)
{
   unsigned int program = glCreateProgram(); // Create a shader program
   unsigned int vs = CompileShader(GL_VERTEX_SHADER, vertexShader);
   unsigned int fs = CompileShader(GL_FRAGMENT_SHADER, fragmentShader);

   glAttachShader(program, vs);
   glAttachShader(program, fs);
   glLinkProgram(program);
   glValidateProgram(program);

   glDeleteShader(vs);
   glDeleteShader(fs);

   return program;
}

static bool isExtensionSupported(const char *extList, const char *extension) {
	const char *start;
	const char *where, *terminator;

	where = strchr(extension, ' ');
	if (where || *extension == '\0') {
		return false;
	}

	for (start=extList;;) {
		where = strstr(start, extension);

		if (!where) {
		 	break;
		}

		terminator = where + strlen(extension);

		if ( where == start || *(where - 1) == ' ' ) {
			if ( *terminator == ' ' || *terminator == '\0' ) {
				return true;
			}
		}

		start = terminator;
	}

	return false;
}

int main(int argc, char **argv)
{
   /*** Setup X11 window ***/

   // Open the display
   dpy = XOpenDisplay(NULL); // NULL = first monitor
   if (dpy == NULL)
   {
      std::cerr << "Cannot connect to X server" << std::endl;
      exit(-1);
   }
   scrn = DefaultScreenOfDisplay(dpy);
   scrn_id = DefaultScreen(dpy);

   // Check GLX version
   int vmajor, vminor = 0;
   glXQueryVersion(dpy, &vmajor, &vminor);

   // Specify what version of OpenGL we're using to the X11 extension (330 Core)
   int glx_attribs[] = {
      GLX_X_RENDERABLE    , True,
      GLX_DRAWABLE_TYPE   , GLX_WINDOW_BIT,
      GLX_RENDER_TYPE     , GLX_RGBA_BIT,
      GLX_X_VISUAL_TYPE   , GLX_TRUE_COLOR,
      GLX_RED_SIZE        , 8,
      GLX_GREEN_SIZE      , 8,
      GLX_BLUE_SIZE       , 8,
      GLX_ALPHA_SIZE      , 8,
      GLX_DEPTH_SIZE      , 24,
      GLX_STENCIL_SIZE    , 8,
      /*
       * NOTE: The buffer swap for double buffering is synchronized with your monitor's
       * vertical refresh rate (v-sync). Disabling double buffering effectively
       * unlocks the framerate as the buffer swaps no longer need to align with v-sync.
       */
      GLX_DOUBLEBUFFER    , True,
      None
   };

   // Create a framebuffer configuration
   int fbcount;
   GLXFBConfig *fbc = glXChooseFBConfig(dpy, win, glx_attribs, &fbcount);
   if (fbc == NULL)
   {
      std::cerr << "Failed to retrieve framebuffer\n" << std::endl;
      XCloseDisplay(dpy);
      exit(-1);
   }

   // Pick the FB config/visual with the most samples per pixel
	std::cout << "Getting best XVisualInfo\n";
	int best_fbc = -1, worst_fbc = -1, best_num_samp = -1, worst_num_samp = 999;
	for (int i = 0; i < fbcount; i++) {
		vi = glXGetVisualFromFBConfig(dpy, fbc[i]);
		if (vi != 0) {
			int samp_buf, samples;
			glXGetFBConfigAttrib(dpy, fbc[i], GLX_SAMPLE_BUFFERS, &samp_buf);
			glXGetFBConfigAttrib(dpy, fbc[i], GLX_SAMPLES, &samples);

			if (best_fbc < 0 || (samp_buf && samples > best_num_samp)) {
				best_fbc = i;
				best_num_samp = samples;
			}
			if (worst_fbc < 0 || !samp_buf || samples < worst_num_samp)
				worst_fbc = i;
			worst_num_samp = samples;
		}
		XFree(vi);
	}
	std::cout << "Best visual info index: " << best_fbc << std::endl;
	GLXFBConfig bestFbc = fbc[best_fbc];
	XFree( fbc ); // Make sure to free this!

   vi = glXGetVisualFromFBConfig(dpy, bestFbc);
   if (vi == NULL)
   {
      std::cerr << "No appropriate visual found" << std::endl;
      exit(-1);
   }
   if (scrn_id != vi->screen) {
      std::cout << "scrn_id(" << scrn_id << ") does not match vi->screen(" << vi->screen << ")" << std::endl;
      exit(-1);
   }

   // Open the window
	XSetWindowAttributes windowAttribs;
	windowAttribs.border_pixel = BlackPixel(dpy, scrn_id);
	windowAttribs.background_pixel = WhitePixel(dpy, scrn_id);
	windowAttribs.override_redirect = true;
	windowAttribs.colormap = XCreateColormap(dpy, RootWindow(dpy, scrn_id), vi->visual, AllocNone);
	windowAttribs.event_mask = ExposureMask;
	win = XCreateWindow(dpy, RootWindow(dpy, scrn_id), 0, 0, 600, 600, 0, vi->depth, InputOutput, vi->visual, CWBackPixel | CWColormap | CWBorderPixel | CWEventMask, &windowAttribs);

	// Create GLX OpenGL context
	glXCreateContextAttribsARBProc glXCreateContextAttribsARB = 0;
	glXCreateContextAttribsARB = (glXCreateContextAttribsARBProc) glXGetProcAddressARB( (const GLubyte *) "glXCreateContextAttribsARB" );

	const char *glxExts = glXQueryExtensionsString(dpy, scrn_id);
	std::cout << "Late extensions:\n\t" << glxExts << std::endl;
	if (glXCreateContextAttribsARB == 0) {
		std::cout << "glXCreateContextAttribsARB() not found" << std::endl;
	}

	int context_attribs[] = {
		GLX_CONTEXT_MAJOR_VERSION_ARB, 3,
		GLX_CONTEXT_MINOR_VERSION_ARB, 3,
		GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
		None
	};

   if (!isExtensionSupported(glxExts, "GLX_ARB_create_context")) {
      glx = glXCreateNewContext(dpy, bestFbc, GLX_RGBA_TYPE, 0, true);
   } else {
      glx = glXCreateContextAttribsARB(dpy, bestFbc, 0, true, context_attribs);
   }
   XSync(dpy, false);

   // Verifying that context is a direct context
   if (!glXIsDirect(dpy, glx)) {
      std::cout << "Indirect GLX rendering context obtained" << std::endl;
   } else {
      std::cout << "Direct GLX rendering context obtained" << std::endl;
   }
   glXMakeCurrent(dpy, win, glx);

#ifdef DEBUG
   std::cout << "GL Vendor: " << glGetString(GL_VENDOR) << "\n";
	std::cout << "GL Renderer: " << glGetString(GL_RENDERER) << "\n";
	std::cout << "GL Version: " << glGetString(GL_VERSION) << "\n";
	std::cout << "GL Shading Language: " << glGetString(GL_SHADING_LANGUAGE_VERSION) << "\n";
#endif

   // Handle the following events:
   //swa.event_mask = (ExposureMask | KeyPressMask | ButtonPressMask | ButtonReleaseMask);

   XClearWindow(dpy, win);
   XMapRaised(dpy, win);

   // Must be placed after a valid OpenGL context has been made current
   if (glewInit() != GLEW_OK)
   {
      std::cerr << "Failed to initialize GLEW" << std::endl;
      exit(-1);
   }

   /*** Setup debugging ***/

   // NOTE: You cannot OR these!!!
   glEnable(GL_DEPTH_TEST);
   glEnable(GL_DEBUG_OUTPUT);
   glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
   if (glDebugMessageCallback)
      glDebugMessageCallback(debugCallback, nullptr); // Set callback function for debug messages
   else
      std::cout << "WARNING: glDebugMessageCallback() is unavailable!" << std::endl;

   /*** Setup VAO, VBO, and EBO ***/

   float vertices[] = {
     // positions         // colors
     0.5f,  0.5f,  0.0f,  0.0f, 0.0f, 1.0f,   // top right
     0.5f, -0.5f,  0.0f,  1.0f, 0.0f, 0.0f,   // bottom right
    -0.5f, -0.5f,  0.0f,  0.0f, 1.0f, 0.0f,   // bottom left
    -0.5f,  0.5f,  0.0f,  0.5f, 0.5f, 0.5f    // top left
   };

   /*
    * 3--0
    * | /|
    * |/ |
    * 2--1
    */
   unsigned int indices[6] = {
      0, 1, 3,
      1, 2, 3
   };

   unsigned int vao, vbo, ebo;
   glGenVertexArrays(1, &vao);
   glGenBuffers(1, &vbo);
   glGenBuffers(1, &ebo);

   glBindVertexArray(vao);

   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

   glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
   glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

   // Position attribute
   glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
   glEnableVertexAttribArray(0);

   // Color attribute
   glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
   glEnableVertexAttribArray(1);

   // Unbind array buffer + vertex array
   glBindBuffer(GL_ARRAY_BUFFER, 0);
   glBindVertexArray(0);

   // Uncomment for wireframe
   //glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

   /*** Setup vertex/fragment shaders ***/

   std::ifstream ifs("res/shader/vertex.shader");
   const std::string vertexShader((std::istreambuf_iterator<char>(ifs)),
                                  (std::istreambuf_iterator<char>()));
   ifs.close();
   ifs.open("res/shader/fragment.shader");
   const std::string fragmentShader((std::istreambuf_iterator<char>(ifs)),
                                    (std::istreambuf_iterator<char>()));
   std::cout << fragmentShader << std::endl;
   ifs.close();

   unsigned int shader = CreateShader(vertexShader, fragmentShader);

   float theta_rad = 0.0f;
   float theta_deg = 0.0f;
   float t_fact = 0.0f;

   mat4 trn_mat = { 0 };
   mat4 rot_mat = { 0 };
   mat4 mvp_mat = { 0 };

   int _fpsCount = 0;
   int fps = 0; // this will store the final fps for the last second

   using namespace std::chrono;
   time_point<steady_clock> lastTime = steady_clock::now();

   /*** Game loop ***/

   while (true)
   {
      /*** Process events ***/

      while (XPending(dpy) > 0)
      {
         XNextEvent(dpy, &xev); // Blocks until event is received

         switch (xev.type)
         {
            case Expose: // Window was being overlapped by another window, but is now exposed
            {
               /* Set affine transform for viewport based on window width/height */
               XGetWindowAttributes(dpy, win, &gwa);
               glViewport(0, 0, gwa.width, gwa.height);

               std::cout << "Window was exposed" << std::endl;
               break;
            }
            case KeyPress:
            {
               std::cout << "Key press detected" << std::endl;
               break;
            }
            case ButtonPress: // Mouse button pressed
            {
               // TODO: Not working
               t_fact += 0.1;
               std::cout << "Click detected" << std::endl;
               break;
            }
         }
      }

      /*** Render ***/

      glClearColor(0.2f, 0.4f, 0.4f, 1.0);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

      glUseProgram(shader); // Bind shader program for draw call
      glBindVertexArray(vao);

      theta_deg += (1 % 360);
      theta_rad = lac_deg_to_rad(theta_deg);

      lac_get_rotation_mat4(&rot_mat, theta_rad, theta_rad, theta_rad);
      lac_get_translation_mat4(&trn_mat, t_fact, t_fact/2, t_fact/4);
      lac_dot_prod_mat4(trn_mat, rot_mat, &mvp_mat);

      t_fact -= 0.01;

      int mvpLocation = glGetUniformLocation(shader, "model");
      // Change to GL_TRUE if column major ordering is required
      glUniformMatrix4fv(mvpLocation, 1, GL_TRUE, mvp_mat);

      glm::mat4 view = glm::lookAt(
          glm::vec3(1.2f, 1.2f, 1.2f),
          glm::vec3(0.0f, 0.0f, 0.0f),
          glm::vec3(0.0f, 0.0f, 1.0f)
      );
      GLint uniView = glGetUniformLocation(shader, "view");
      glUniformMatrix4fv(uniView, 1, GL_FALSE, glm::value_ptr(view));

      glm::mat4 proj = glm::perspective(glm::radians(45.0f), 800.0f / 600.0f, 1.0f, 10.0f);
      GLint uniProj = glGetUniformLocation(shader, "proj");
      glUniformMatrix4fv(uniProj, 1, GL_FALSE, glm::value_ptr(proj));

      //std::cout << "millis now = " << millis_now << std::endl;

      glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
      //glDrawArrays(GL_TRIANGLES, 0, 3);

      glXSwapBuffers(dpy, win);

      CalculateFrameRate(fps, _fpsCount, lastTime);
   }

   glDeleteVertexArrays(1, &vao);
   glDeleteBuffers(1, &vbo);
   glDeleteBuffers(1, &ebo);
   glDeleteProgram(shader);

   XDestroyWindow(dpy, win);
   XCloseDisplay(dpy);

   return 0;
}
