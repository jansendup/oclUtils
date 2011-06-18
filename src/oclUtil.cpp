#include <stdio.h>
#include <string>
#include <string.h>

#include <CL/cl.h>
#include <oclUtil.h>

#ifdef OCL_UTIL_GL_SHARING_ENABLE
#include "opengl.h"

// OpenGL utility toolkit includes
#if defined (__APPLE__) || defined(MACOSX)
    #include <OpenGL/OpenGL.h>
    #include <GLUT/glut.h>
#else
    #include <GL/freeglut.h>
    #ifdef UNIX
       #include <GL/glx.h>
    #endif
#endif

// OpenGL/OpenCL interop includes and defines
#include <CL/cl_gl.h>

#if defined (__APPLE__) || defined(MACOSX)
#define GL_SHARING_EXTENSION "cl_APPLE_gl_sharing"
#else
#define GL_SHARING_EXTENSION "cl_khr_gl_sharing"
#endif

#if defined __APPLE__ || defined(MACOSX)
#else
#if defined WIN32
#else
//needed for context sharing functions
#include <GL/glx.h>
#endif
#endif
#endif

char *oclLoadProgramContents(const char *filename, int *length)
{
	FILE *f = fopen(filename, "r");
	void *buffer;

	if (!f) {
		printf("Unable to open %s for reading\n", filename);
		return NULL;
	}

	fseek(f, 0, SEEK_END);
	*length = ftell(f);
	fseek(f, 0, SEEK_SET);

	buffer = malloc(*length+1);
	*length = fread(buffer, 1, *length, f);
	fclose(f);
	((char*)buffer)[*length] = '\0';

	return (char*)buffer;
}

#ifdef OCL_UTIL_GL_SHARING_ENABLE
GLuint oglCreateVBO(const void* data, int dataSize, GLenum target, GLenum usage)
{
	GLuint vbo;
	glGenBuffers(1,&vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(target, dataSize, data, usage);

	int bufferSize = 0;
	glGetBufferParameteriv(target, GL_BUFFER_SIZE, &bufferSize);
	if( dataSize != bufferSize )
	{
		printf("No memmory allocated for gl buffer.\n");
		glDeleteBuffers(1, &vbo);
		vbo = 0;
	}
	
	glBindBuffer(target, 0); // Unbind buffer
	return vbo;
}
#endif

bool oclGetNVIDIAPlatform(cl_platform_id* clSelectedPlatformID)
{
	char chBuffer[1024];
	cl_uint num_platforms;
	cl_platform_id* clPlatformIDs;
	cl_int error;
	*clSelectedPlatformID = NULL;
	cl_uint i = 0;

	// Get OpenCL platform count
	error = clGetPlatformIDs (0, NULL, &num_platforms);
	if ( !oclHandleErrorMessage("Getting platforms",error) )
		return false;
	
	if(num_platforms == 0)
	{
		printf("No OpenCL platform was found!\n");
		return false;
	}
	else
	{
		// if there's a platform or more, make space for ID's
		if ((clPlatformIDs = (cl_platform_id*)malloc(num_platforms * sizeof(cl_platform_id))) == NULL)
		{
			printf("Failed to allocate memory for cl_platform ID's!\n");
			return false;
		}

		// get platform info for each platform and trap the NVIDIA platform if found
		error = clGetPlatformIDs (num_platforms, clPlatformIDs, NULL);
		printf("Available platforms:\n");
		for(i = 0; i < num_platforms; ++i)
		{
			error = clGetPlatformInfo (clPlatformIDs[i], CL_PLATFORM_NAME, 1024, &chBuffer, NULL);
			if(error == CL_SUCCESS)
			{
				printf("Platform %d: %s\n", i, chBuffer);
				if(strstr(chBuffer, "NVIDIA") != NULL)
				{
					printf("Selected platform %d\n", i);
					*clSelectedPlatformID = clPlatformIDs[i];
					break;
				}
			}
		}

		// default to first platform if NVIDIA not found
		if(*clSelectedPlatformID == NULL)
		{
			printf("Could not find a nvidia platform.\n Defaulting to first found platform.\n");
			printf("selected platform: %d\n", 0);
			*clSelectedPlatformID = clPlatformIDs[0];
		}

		free(clPlatformIDs);
	}

	return true;
}

bool oclGetSomeGPUDevice(cl_device_id* deviceId , cl_platform_id platformId)
{
	cl_uint deviceCount;
	cl_int error;
	cl_device_id* devices;

	// Get number of devices
	error = clGetDeviceIDs(platformId,CL_DEVICE_TYPE_GPU,0, NULL, &deviceCount);
	if( !oclHandleErrorMessage("Fetching GPU device count",error) ) return false;

	if(deviceCount == 0)
	{
		printf("No GPU devices found on system\n");
		return false;
	}

	// Get list of devices
	devices = (cl_device_id*)malloc(deviceCount*sizeof(cl_device_id));
	error = clGetDeviceIDs(platformId,CL_DEVICE_TYPE_GPU, deviceCount, devices, NULL);
	if( !oclHandleErrorMessage("Geting list of GPU devices",error) ) return false;

#ifdef OCL_UTIL_GL_SHARING_ENABLE
	// Search for device that supports context sharing.
	bool foundDevice = false;
	int deviceIndex;
	for(int i = 0; i < deviceCount; i++)
	{
		size_t extensionsSize;

		// Get size of extensions
		error = clGetDeviceInfo(devices[i], CL_DEVICE_EXTENSIONS, 0, NULL, &extensionsSize);
		if( !oclHandleErrorMessage("Getting device extensions count",error) ) return false;

		// Get extensions
		char* extensions = (char*)malloc(extensionsSize);
		error = clGetDeviceInfo(devices[i], CL_DEVICE_EXTENSIONS, extensionsSize, extensions, NULL);
		if( !oclHandleErrorMessage("Getting device extensions",error) ) { free(extensions); continue;}

		// Check if the extensions contains the GL_SHARING_EXTENSION
		if( strstr(extensions,GL_SHARING_EXTENSION) != NULL )
		{
			printf("Device %d supports \"%s\".\n",i,GL_SHARING_EXTENSION);
			foundDevice = true;
			deviceIndex = i;
			free(extensions);
			break;
		}
		free(extensions);
	}

	if(!foundDevice)
	{
		printf("Couldn't find a GPU device supporting \"%s\"\n", GL_SHARING_EXTENSION);
		return false;
	}
	printf("Selected device %d.\n",deviceIndex);
	*deviceId = devices[deviceIndex];
#else
	printf("Selected device 0.\n");
	*deviceId = devices[0];
#endif

	free(devices);
	return true;
}

bool oclCreateSomeContext(cl_context* context , cl_device_id deviceId,cl_platform_id platformId)
{
	cl_int error = 0;

#ifdef OCL_UTIL_GL_SHARING_ENABLE
	// Define OS-specific context properties and create the OpenCL context
#if defined (__APPLE__)
	CGLContextObj kCGLContext = CGLGetCurrentContext();
	CGLShareGroupObj kCGLShareGroup = CGLGetShareGroup(kCGLContext);
	if( kCGLContext == NULL)
		printf("CGLGetCurrentContext() returned NULL\n");
	if( kCGLShareGroup == NULL)
		printf("CGLGetShareGroup(kCGLContext) returned NULL\n");
	cl_context_properties props[] = 
	{
		CL_CONTEXT_PROPERTY_USE_CGL_SHAREGROUP_APPLE, (cl_context_properties)kCGLShareGroup, 
		0 
	};
	cxGPUContext = clCreateContext(props, 0,0, NULL, NULL, &error);
#else
#ifdef UNIX
	GLXContext glxContext = glXGetCurrentContext();
	Display* display = glXGetCurrentDisplay();
	if(glxContext == NULL)
		printf("glXGetCurrentContext() returned NULL\n");
	if(display == NULL)
		printf("glXGetCurrentDisplay() returned NULL\n");
	cl_context_properties props[] = 
	{
		CL_GL_CONTEXT_KHR, (cl_context_properties)glxContext, 
		CL_GLX_DISPLAY_KHR, (cl_context_properties)display, 
		CL_CONTEXT_PLATFORM, (cl_context_properties)cpPlatform, 
		0
	};
	cxGPUContext = clCreateContext(props, 1, &cdDevices[uiDeviceUsed], NULL, NULL, &error);
#else // Win32
	HGLRC wglContext = wglGetCurrentContext();
	HDC wglDC = wglGetCurrentDC();
	if(wglContext == NULL)
		printf("wglGetCurrentContext() returned NULL\n");
	if(wglDC == NULL)
		printf("wglGetCurrentDC() returned NULL\n");
	cl_context_properties props[] = 
	{
		CL_GL_CONTEXT_KHR, (cl_context_properties)wglContext, 
		CL_WGL_HDC_KHR, (cl_context_properties)wglDC, 
		CL_CONTEXT_PLATFORM, (cl_context_properties)platformId, 
		0
	};

	*context = clCreateContext(props, 1, &deviceId, NULL, NULL, &error);
	if( !oclHandleErrorMessage("Creating GL-CL shared context", error) ) return false;
#endif
#endif

#else
	*context = clCreateContext(NULL, 1, &deviceId, NULL, NULL, &error);
	if( !oclHandleErrorMessage("Creating CL context", error) ) return false;
#endif
	return true;
}

const char* oclErrorString(cl_int error)
{
	static const char* errorString[] = {
		"CL_SUCCESS",
		"CL_DEVICE_NOT_FOUND",
		"CL_DEVICE_NOT_AVAILABLE",
		"CL_COMPILER_NOT_AVAILABLE",
		"CL_MEM_OBJECT_ALLOCATION_FAILURE",
		"CL_OUT_OF_RESOURCES",
		"CL_OUT_OF_HOST_MEMORY",
		"CL_PROFILING_INFO_NOT_AVAILABLE",
		"CL_MEM_COPY_OVERLAP",
		"CL_IMAGE_FORMAT_MISMATCH",
		"CL_IMAGE_FORMAT_NOT_SUPPORTED",
		"CL_BUILD_PROGRAM_FAILURE",
		"CL_MAP_FAILURE",
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		"CL_INVALID_VALUE",
		"CL_INVALID_DEVICE_TYPE",
		"CL_INVALID_PLATFORM",
		"CL_INVALID_DEVICE",
		"CL_INVALID_CONTEXT",
		"CL_INVALID_QUEUE_PROPERTIES",
		"CL_INVALID_COMMAND_QUEUE",
		"CL_INVALID_HOST_PTR",
		"CL_INVALID_MEM_OBJECT",
		"CL_INVALID_IMAGE_FORMAT_DESCRIPTOR",
		"CL_INVALID_IMAGE_SIZE",
		"CL_INVALID_SAMPLER",
		"CL_INVALID_BINARY",
		"CL_INVALID_BUILD_OPTIONS",
		"CL_INVALID_PROGRAM",
		"CL_INVALID_PROGRAM_EXECUTABLE",
		"CL_INVALID_KERNEL_NAME",
		"CL_INVALID_KERNEL_DEFINITION",
		"CL_INVALID_KERNEL",
		"CL_INVALID_ARG_INDEX",
		"CL_INVALID_ARG_VALUE",
		"CL_INVALID_ARG_SIZE",
		"CL_INVALID_KERNEL_ARGS",
		"CL_INVALID_WORK_DIMENSION",
		"CL_INVALID_WORK_GROUP_SIZE",
		"CL_INVALID_WORK_ITEM_SIZE",
		"CL_INVALID_GLOBAL_OFFSET",
		"CL_INVALID_EVENT_WAIT_LIST",
		"CL_INVALID_EVENT",
		"CL_INVALID_OPERATION",
		"CL_INVALID_GL_OBJECT",
		"CL_INVALID_BUFFER_SIZE",
		"CL_INVALID_MIP_LEVEL",
		"CL_INVALID_GLOBAL_WORK_SIZE",
	};

	const int errorCount = sizeof(errorString) / sizeof(errorString[0]);

	const int index = -error;

	return (index >= 0 && index < errorCount) ? errorString[index] : "";
}

bool oclHandleErrorMessage(const char* action, cl_int error)
{
	printf(action);
	printf("...\t");
	if(error == CL_SUCCESS)
	{
		printf("OK\n");
		return true;
	}
	else
	{
		printf("FAIL\n");
		printf("Error code %d : %s\n", error, oclErrorString(error));
		return false;
	}
}

void oclPrintPlatformInfo(cl_platform_id id)
{
	char chBuffer[1024];
	cl_int error;

	// PROFILE
	error = clGetPlatformInfo (id, CL_PLATFORM_PROFILE, 1024, &chBuffer, NULL);
	if(error == CL_SUCCESS)
	{
		printf("Platform profile: %s\n", chBuffer);
	}
	// VERSION 
	error = clGetPlatformInfo (id, CL_PLATFORM_VERSION, 1024, &chBuffer, NULL);
	if(error == CL_SUCCESS)
	{
		printf("Platform version: %s\n", chBuffer);
	}
	// NAME
	error = clGetPlatformInfo (id, CL_PLATFORM_NAME, 1024, &chBuffer, NULL);
	if(error == CL_SUCCESS)
	{
		printf("Platform name: %s\n", chBuffer);
	}
	// VENDOR
	error = clGetPlatformInfo (id, CL_PLATFORM_VENDOR, 1024, &chBuffer, NULL);
	if(error == CL_SUCCESS)
	{
		printf("Platform vendor: %s\n", chBuffer);
	}
	// EXTENSIONS
	error = clGetPlatformInfo (id, CL_PLATFORM_EXTENSIONS, 1024, &chBuffer, NULL);
	if(error == CL_SUCCESS)
	{
		printf("Platform extensions: %s\n", chBuffer);
	}
}

void oclPrintDeviceInfo(cl_device_id device)
{
	char device_string[1024];
	bool nv_device_attibute_query = false;

	// CL_DEVICE_NAME
	clGetDeviceInfo(device, CL_DEVICE_NAME, sizeof(device_string), &device_string, NULL);
	printf( "  CL_DEVICE_NAME: \t\t\t%s\n", device_string);

	// CL_DEVICE_VENDOR
	clGetDeviceInfo(device, CL_DEVICE_VENDOR, sizeof(device_string), &device_string, NULL);
	printf( "  CL_DEVICE_VENDOR: \t\t\t%s\n", device_string);

	// CL_DRIVER_VERSION
	clGetDeviceInfo(device, CL_DRIVER_VERSION, sizeof(device_string), &device_string, NULL);
	printf( "  CL_DRIVER_VERSION: \t\t\t%s\n", device_string);

	// CL_DEVICE_VERSION
	clGetDeviceInfo(device, CL_DEVICE_VERSION, sizeof(device_string), &device_string, NULL);
	printf( "  CL_DEVICE_VERSION: \t\t\t%s\n", device_string);

#if !defined(__APPLE__) && !defined(__MACOSX)
	// CL_DEVICE_OPENCL_C_VERSION (if CL_DEVICE_VERSION version > 1.0)
	if(strncmp("OpenCL 1.0", device_string, 10) != 0) 
	{
		// This code is unused for devices reporting OpenCL 1.0, but a def is needed anyway to allow compilation using v 1.0 headers 
		// This constant isn't #defined in 1.0
#ifndef CL_DEVICE_OPENCL_C_VERSION
#define CL_DEVICE_OPENCL_C_VERSION 0x103D   
#endif

		clGetDeviceInfo(device, CL_DEVICE_OPENCL_C_VERSION, sizeof(device_string), &device_string, NULL);
		printf( "  CL_DEVICE_OPENCL_C_VERSION: \t\t%s\n", device_string);
	}
#endif

	// CL_DEVICE_TYPE
	cl_device_type type;
	clGetDeviceInfo(device, CL_DEVICE_TYPE, sizeof(type), &type, NULL);
	if( type & CL_DEVICE_TYPE_CPU )
		printf( "  CL_DEVICE_TYPE:\t\t\t%s\n", "CL_DEVICE_TYPE_CPU");
	if( type & CL_DEVICE_TYPE_GPU )
		printf( "  CL_DEVICE_TYPE:\t\t\t%s\n", "CL_DEVICE_TYPE_GPU");
	if( type & CL_DEVICE_TYPE_ACCELERATOR )
		printf( "  CL_DEVICE_TYPE:\t\t\t%s\n", "CL_DEVICE_TYPE_ACCELERATOR");
	if( type & CL_DEVICE_TYPE_DEFAULT )
		printf( "  CL_DEVICE_TYPE:\t\t\t%s\n", "CL_DEVICE_TYPE_DEFAULT");

	// CL_DEVICE_MAX_COMPUTE_UNITS
	cl_uint compute_units;
	clGetDeviceInfo(device, CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(compute_units), &compute_units, NULL);
	printf( "  CL_DEVICE_MAX_COMPUTE_UNITS:\t\t%u\n", compute_units);

	// CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS
	size_t workitem_dims;
	clGetDeviceInfo(device, CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS, sizeof(workitem_dims), &workitem_dims, NULL);
	printf( "  CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS:\t%u\n", workitem_dims);

	// CL_DEVICE_MAX_WORK_ITEM_SIZES
	size_t workitem_size[3];
	clGetDeviceInfo(device, CL_DEVICE_MAX_WORK_ITEM_SIZES, sizeof(workitem_size), &workitem_size, NULL);
	printf( "  CL_DEVICE_MAX_WORK_ITEM_SIZES:\t%u / %u / %u \n", workitem_size[0], workitem_size[1], workitem_size[2]);

	// CL_DEVICE_MAX_WORK_GROUP_SIZE
	size_t workgroup_size;
	clGetDeviceInfo(device, CL_DEVICE_MAX_WORK_GROUP_SIZE, sizeof(workgroup_size), &workgroup_size, NULL);
	printf( "  CL_DEVICE_MAX_WORK_GROUP_SIZE:\t%u\n", workgroup_size);

	// CL_DEVICE_MAX_CLOCK_FREQUENCY
	cl_uint clock_frequency;
	clGetDeviceInfo(device, CL_DEVICE_MAX_CLOCK_FREQUENCY, sizeof(clock_frequency), &clock_frequency, NULL);
	printf( "  CL_DEVICE_MAX_CLOCK_FREQUENCY:\t%u MHz\n", clock_frequency);

	// CL_DEVICE_ADDRESS_BITS
	cl_uint addr_bits;
	clGetDeviceInfo(device, CL_DEVICE_ADDRESS_BITS, sizeof(addr_bits), &addr_bits, NULL);
	printf( "  CL_DEVICE_ADDRESS_BITS:\t\t%u\n", addr_bits);

	// CL_DEVICE_MAX_MEM_ALLOC_SIZE
	cl_ulong max_mem_alloc_size;
	clGetDeviceInfo(device, CL_DEVICE_MAX_MEM_ALLOC_SIZE, sizeof(max_mem_alloc_size), &max_mem_alloc_size, NULL);
	printf( "  CL_DEVICE_MAX_MEM_ALLOC_SIZE:\t\t%u MByte\n", (unsigned int)(max_mem_alloc_size / (1024 * 1024)));

	// CL_DEVICE_GLOBAL_MEM_SIZE
	cl_ulong mem_size;
	clGetDeviceInfo(device, CL_DEVICE_GLOBAL_MEM_SIZE, sizeof(mem_size), &mem_size, NULL);
	printf( "  CL_DEVICE_GLOBAL_MEM_SIZE:\t\t%u MByte\n", (unsigned int)(mem_size / (1024 * 1024)));

	// CL_DEVICE_ERROR_CORRECTION_SUPPORT
	cl_bool error_correction_support;
	clGetDeviceInfo(device, CL_DEVICE_ERROR_CORRECTION_SUPPORT, sizeof(error_correction_support), &error_correction_support, NULL);
	printf( "  CL_DEVICE_ERROR_CORRECTION_SUPPORT:\t%s\n", error_correction_support == CL_TRUE ? "yes" : "no");

	// CL_DEVICE_LOCAL_MEM_TYPE
	cl_device_local_mem_type local_mem_type;
	clGetDeviceInfo(device, CL_DEVICE_LOCAL_MEM_TYPE, sizeof(local_mem_type), &local_mem_type, NULL);
	printf( "  CL_DEVICE_LOCAL_MEM_TYPE:\t\t%s\n", local_mem_type == 1 ? "local" : "global");

	// CL_DEVICE_LOCAL_MEM_SIZE
	clGetDeviceInfo(device, CL_DEVICE_LOCAL_MEM_SIZE, sizeof(mem_size), &mem_size, NULL);
	printf( "  CL_DEVICE_LOCAL_MEM_SIZE:\t\t%u KByte\n", (unsigned int)(mem_size / 1024));

	// CL_DEVICE_MAX_CONSTANT_BUFFER_SIZE
	clGetDeviceInfo(device, CL_DEVICE_MAX_CONSTANT_BUFFER_SIZE, sizeof(mem_size), &mem_size, NULL);
	printf( "  CL_DEVICE_MAX_CONSTANT_BUFFER_SIZE:\t%u KByte\n", (unsigned int)(mem_size / 1024));

	// CL_DEVICE_QUEUE_PROPERTIES
	cl_command_queue_properties queue_properties;
	clGetDeviceInfo(device, CL_DEVICE_QUEUE_PROPERTIES, sizeof(queue_properties), &queue_properties, NULL);
	if( queue_properties & CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE )
		printf( "  CL_DEVICE_QUEUE_PROPERTIES:\t\t%s\n", "CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE");    
	if( queue_properties & CL_QUEUE_PROFILING_ENABLE )
		printf( "  CL_DEVICE_QUEUE_PROPERTIES:\t\t%s\n", "CL_QUEUE_PROFILING_ENABLE");

	// CL_DEVICE_IMAGE_SUPPORT
	cl_bool image_support;
	clGetDeviceInfo(device, CL_DEVICE_IMAGE_SUPPORT, sizeof(image_support), &image_support, NULL);
	printf( "  CL_DEVICE_IMAGE_SUPPORT:\t\t%u\n", image_support);

	// CL_DEVICE_MAX_READ_IMAGE_ARGS
	cl_uint max_read_image_args;
	clGetDeviceInfo(device, CL_DEVICE_MAX_READ_IMAGE_ARGS, sizeof(max_read_image_args), &max_read_image_args, NULL);
	printf( "  CL_DEVICE_MAX_READ_IMAGE_ARGS:\t%u\n", max_read_image_args);

	// CL_DEVICE_MAX_WRITE_IMAGE_ARGS
	cl_uint max_write_image_args;
	clGetDeviceInfo(device, CL_DEVICE_MAX_WRITE_IMAGE_ARGS, sizeof(max_write_image_args), &max_write_image_args, NULL);
	printf( "  CL_DEVICE_MAX_WRITE_IMAGE_ARGS:\t%u\n", max_write_image_args);

	// CL_DEVICE_SINGLE_FP_CONFIG
	cl_device_fp_config fp_config;
	clGetDeviceInfo(device, CL_DEVICE_SINGLE_FP_CONFIG, sizeof(cl_device_fp_config), &fp_config, NULL);
	printf( "  CL_DEVICE_SINGLE_FP_CONFIG:\t\t%s%s%s%s%s%s\n",
		fp_config & CL_FP_DENORM ? "denorms " : "",
		fp_config & CL_FP_INF_NAN ? "INF-quietNaNs " : "",
		fp_config & CL_FP_ROUND_TO_NEAREST ? "round-to-nearest " : "",
		fp_config & CL_FP_ROUND_TO_ZERO ? "round-to-zero " : "",
		fp_config & CL_FP_ROUND_TO_INF ? "round-to-inf " : "",
		fp_config & CL_FP_FMA ? "fma " : "");

	// CL_DEVICE_IMAGE2D_MAX_WIDTH, CL_DEVICE_IMAGE2D_MAX_HEIGHT, CL_DEVICE_IMAGE3D_MAX_WIDTH, CL_DEVICE_IMAGE3D_MAX_HEIGHT, CL_DEVICE_IMAGE3D_MAX_DEPTH
	size_t szMaxDims[5];
	printf( "\n  CL_DEVICE_IMAGE <dim>"); 
	clGetDeviceInfo(device, CL_DEVICE_IMAGE2D_MAX_WIDTH, sizeof(size_t), &szMaxDims[0], NULL);
	printf( "\t\t\t2D_MAX_WIDTH\t %u\n", szMaxDims[0]);
	clGetDeviceInfo(device, CL_DEVICE_IMAGE2D_MAX_HEIGHT, sizeof(size_t), &szMaxDims[1], NULL);
	printf( "\t\t\t\t\t2D_MAX_HEIGHT\t %u\n", szMaxDims[1]);
	clGetDeviceInfo(device, CL_DEVICE_IMAGE3D_MAX_WIDTH, sizeof(size_t), &szMaxDims[2], NULL);
	printf( "\t\t\t\t\t3D_MAX_WIDTH\t %u\n", szMaxDims[2]);
	clGetDeviceInfo(device, CL_DEVICE_IMAGE3D_MAX_HEIGHT, sizeof(size_t), &szMaxDims[3], NULL);
	printf( "\t\t\t\t\t3D_MAX_HEIGHT\t %u\n", szMaxDims[3]);
	clGetDeviceInfo(device, CL_DEVICE_IMAGE3D_MAX_DEPTH, sizeof(size_t), &szMaxDims[4], NULL);
	printf( "\t\t\t\t\t3D_MAX_DEPTH\t %u\n", szMaxDims[4]);

	// CL_DEVICE_EXTENSIONS: get device extensions, and if any then parse & log the string onto separate lines
	clGetDeviceInfo(device, CL_DEVICE_EXTENSIONS, sizeof(device_string), &device_string, NULL);
	if (device_string != 0) 
	{
		printf( "\n  CL_DEVICE_EXTENSIONS:");
		std::string stdDevString;
		stdDevString = std::string(device_string);
		size_t szOldPos = 0;
		size_t szSpacePos = stdDevString.find(' ', szOldPos); // extensions string is space delimited
		while (szSpacePos != stdDevString.npos)
		{
			if( strcmp("cl_nv_device_attribute_query", stdDevString.substr(szOldPos, szSpacePos - szOldPos).c_str()) == 0 )
				nv_device_attibute_query = true;

			if (szOldPos > 0)
			{
				printf( "\t\t");
			}
			printf( "\t\t\t%s\n", stdDevString.substr(szOldPos, szSpacePos - szOldPos).c_str());

			do {
				szOldPos = szSpacePos + 1;
				szSpacePos = stdDevString.find(' ', szOldPos);
			} while (szSpacePos == szOldPos);
		}
		printf( "\n");
	}
	else 
	{
		printf( "  CL_DEVICE_EXTENSIONS: None\n");
	}

	// CL_DEVICE_PREFERRED_VECTOR_WIDTH_<type>
	printf( "  CL_DEVICE_PREFERRED_VECTOR_WIDTH_<t>\t"); 
	cl_uint vec_width [6];
	clGetDeviceInfo(device, CL_DEVICE_PREFERRED_VECTOR_WIDTH_CHAR, sizeof(cl_uint), &vec_width[0], NULL);
	clGetDeviceInfo(device, CL_DEVICE_PREFERRED_VECTOR_WIDTH_SHORT, sizeof(cl_uint), &vec_width[1], NULL);
	clGetDeviceInfo(device, CL_DEVICE_PREFERRED_VECTOR_WIDTH_INT, sizeof(cl_uint), &vec_width[2], NULL);
	clGetDeviceInfo(device, CL_DEVICE_PREFERRED_VECTOR_WIDTH_LONG, sizeof(cl_uint), &vec_width[3], NULL);
	clGetDeviceInfo(device, CL_DEVICE_PREFERRED_VECTOR_WIDTH_FLOAT, sizeof(cl_uint), &vec_width[4], NULL);
	clGetDeviceInfo(device, CL_DEVICE_PREFERRED_VECTOR_WIDTH_DOUBLE, sizeof(cl_uint), &vec_width[5], NULL);
	printf( "CHAR %u, SHORT %u, INT %u, LONG %u, FLOAT %u, DOUBLE %u\n\n\n", 
		vec_width[0], vec_width[1], vec_width[2], vec_width[3], vec_width[4], vec_width[5]); 
}

void oclPrintBuildLog(cl_program program, cl_device_id deviceId)
{
	cl_build_status build_status;
	cl_int error;
	
	if(program == NULL)
		return;

	// Get and print build status messages.
	error = clGetProgramBuildInfo(program, deviceId, CL_PROGRAM_BUILD_STATUS, sizeof(cl_build_status), &build_status, NULL);

	char *build_log;
	size_t ret_val_size;
	error = clGetProgramBuildInfo(program, deviceId, CL_PROGRAM_BUILD_LOG, 0, NULL, &ret_val_size);

	build_log = new char[ret_val_size+1];
	error = clGetProgramBuildInfo(program, deviceId, CL_PROGRAM_BUILD_LOG, ret_val_size, build_log, NULL);
	build_log[ret_val_size] = '\0';
	printf("BUILD LOG: \n %s", build_log);

	delete build_log;
}

