// Copyright Eternal Developments LLC. All Rights Reserved.

#include <cmath>
#include <numbers>
#include <Windows.h>

#include "mandelbrot_ispc.h"

using namespace ispc;

static bool bRunning = true;

static const int Iterations = 50;
static const int WindowWidth = 1536;
static const int WindowHeight = 1536;

static HWND WindowHandle;
static uint8_t PalettedMandelbrotImage[WindowWidth * WindowHeight];
static LARGE_INTEGER Duration;

// The custom WndProc to handle clicking to exit
LRESULT CALLBACK WindowProc( HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
	switch( uMsg )
	{
	case WM_LBUTTONDOWN:
	case WM_RBUTTONDOWN:
		bRunning = false;
		break;
	}

	return DefWindowProc( hwnd, uMsg, wParam, lParam );
}

// Creates a simple window with a custom WndProc
HWND CreateSimpleWindow( HINSTANCE hInstance )
{
	WNDCLASS window_class =
	{
		CS_OWNDC,
		WindowProc,
		0,
		0,
		hInstance,
		nullptr,
		nullptr,
		nullptr,
		nullptr,
		L"MandelbrotWindowClass"
	};

	RegisterClass( &window_class );

	HWND handle = CreateWindow( L"MandelbrotWindowClass", L"Mandelbrot", WS_POPUPWINDOW, 0, 0, WindowWidth, WindowHeight, nullptr, nullptr, nullptr, nullptr );

	return handle;
}

// Create a paletted Mandelbot set
static void DrawMandelbrot()
{
	unsigned int width = WindowWidth;
	unsigned int height = WindowHeight;
	float left = -0.75f;
	float right = 0.1f;
	float bottom = -1.1f;
	float top = -0.35f;
	int maxIterations = 256;

	mandelbrot_ispc( left, bottom, right, top, width, height, maxIterations, PalettedMandelbrotImage );
}

// Convert a paletted Mandelbrot set to a bitmap and set that bitmap as the window background
static void DrawBitmap()
{
	int* mandelbrot_palette = new int[256];
	int* mandelbrot_image = new int[WindowWidth * WindowHeight];

	for( int palette_index = 0; palette_index < 256; palette_index++ )
	{
		double offset = palette_index / 256.0;

		const int red = static_cast< int >( 255.0 * ( 0.5 + 0.5 * std::cos( 2.0 * std::numbers::pi * ( offset + 0.00 ) ) ) );
		const int green = static_cast< int >( 255.0 * ( 0.5 + 0.5 * std::cos( 2.0 * std::numbers::pi * ( offset + 0.33 ) ) ) );
		const int blue = static_cast< int >( 255.0 * ( 0.5 + 0.5 * std::cos( 2.0 * std::numbers::pi * ( offset + 0.67 ) ) ) );

		mandelbrot_palette[palette_index] = RGB( red, green, blue ) | 0xff000000;
	}

	for( int index = 0; index < WindowWidth * WindowHeight; index++ )
	{
		int palette_index = PalettedMandelbrotImage[index];
		mandelbrot_image[index] = mandelbrot_palette[palette_index];
	}

	HDC destination_dc = GetDC( WindowHandle );
	HDC source_dc = CreateCompatibleDC( destination_dc );

	BITMAP new_bitmap = { 0 };
	HBITMAP hBitmap = CreateBitmap( WindowWidth, WindowHeight, 1, 32, mandelbrot_image );
	GetObject( hBitmap, sizeof( BITMAP ), ( LPSTR )&new_bitmap );
	SelectObject( source_dc, hBitmap );

	BitBlt( destination_dc, 0, 0, new_bitmap.bmWidth, new_bitmap.bmHeight, source_dc, 0, 0, SRCCOPY );

	DeleteDC( source_dc );
	DeleteDC( destination_dc );
}

// Main program logic
int CALLBACK WinMain( HINSTANCE hInstance, HINSTANCE, LPSTR, int )
{
	WindowHandle = CreateSimpleWindow( hInstance );

	// Recalculate a Mandelbrot set several times to get an average
	Duration.QuadPart = 0;
	for( int iteration = 0; iteration < Iterations; iteration++ )
	{
		LARGE_INTEGER start_time;
		QueryPerformanceCounter( &start_time );

		DrawMandelbrot();

		LARGE_INTEGER end_time;
		QueryPerformanceCounter( &end_time );

		Duration.QuadPart += end_time.QuadPart - start_time.QuadPart;
	}

	ShowWindow( WindowHandle, SW_SHOW );
	DrawBitmap();

	// Tick the input loop waiting for a mouse click
	while( bRunning )
	{
		MSG message;
		while( PeekMessage( &message, nullptr, 0, 0, PM_NOREMOVE ) )
		{
			GetMessage( &message, nullptr, 0, 0 );
			TranslateMessage( &message );
			DispatchMessage( &message );
		}
	}

	// Display the result
	LARGE_INTEGER frequency;
	QueryPerformanceFrequency( &frequency );
	int milliseconds = static_cast< int >( ( Duration.QuadPart * 1000.0 ) / frequency.QuadPart );

	wchar_t results[256] = { 0 };
	wsprintf( results, L"Mandelbrot generation averaged %d ms", milliseconds / Iterations );
	MessageBox( nullptr, results, L"Mandelbrot ISPC Test", MB_OK );

	CloseWindow( WindowHandle );
	return 0;
}
