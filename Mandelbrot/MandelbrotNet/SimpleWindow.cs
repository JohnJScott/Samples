// Copyright Eternal Developments, LLC. All Rights Reserved.

using System.Diagnostics;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using Math = System.Math;

namespace MandelbrotNet
{
	public partial class SimpleWindow
		: Form
	{
		// Constants to tweak
		private const int Iterations = 50;
		private const int WindowWidth = 1536;
		private const int WindowHeight = 1536;

		// The paletted result
		private readonly byte[] PalettedMandelbrotImage = new byte[WindowWidth * WindowHeight];

		private readonly Stopwatch Timer = new Stopwatch();

		// Display the results as the form closes
		private void OnFormClosed( object sender, FormClosingEventArgs e )
		{
			// Show the timing results
			MessageBox.Show( null, "Mandelbrot generation averaged " + Timer.ElapsedMilliseconds / Iterations + " ms", "Mandelbrot C# Test", MessageBoxButtons.OK );
		}

		// A simple click to exit callback
		private void OnMouseClick( object sender, MouseEventArgs e )
		{
			Close();
		}

		// Create a paletted Mandelbot set
		private void CreateMandelbrot()
		{
			float left = -0.75f;
			float right = 0.1f;
			float bottom = -0.35f;
			float top = -1.1f;

			float delta_x = ( right - left ) / WindowWidth;
			float delta_y = ( bottom - top ) / WindowHeight;

			float x_coordinate = left;
			for( int x_index = 0; x_index < WindowWidth; x_index++ )
			{
				float y_coordinate = top;
				for( int y_index = 0; y_index < WindowHeight; y_index++ )
				{
					float work_x = 0;
					float work_y = 0;

					int counter = 0;
					while( counter < 255 && ( ( work_x * work_x ) + ( work_y * work_y ) ) < 4.0 )
					{
						counter++;

						float new_work_x = ( work_x * work_x ) - ( work_y * work_y ) + x_coordinate;
						work_y = 2 * work_x * work_y + y_coordinate;
						work_x = new_work_x;
					}

					PalettedMandelbrotImage[( y_index * WindowWidth ) + x_index] = ( byte )counter;

					y_coordinate += delta_y;
				}

				x_coordinate += delta_x;
			}
		}

		// Convert a paletted Mandelbrot set to a bitmap and set that bitmap as the window background
		private void DrawBitmap()
		{
			Color[] mandelbrot_palette = new Color[256];
			int[] mandelbrot_image = new int[WindowWidth * WindowHeight];

			for( int palette_index = 0; palette_index < 256; palette_index++ )
			{
				double offset = palette_index / 256.0;

				int red = ( int )( 255.0 * ( 0.5 + 0.5 * Math.Cos( 2.0 * Math.PI * ( offset + 0.00 ) ) ) );
				int green = ( int )( 255.0 * ( 0.5 + 0.5 * Math.Cos( 2.0 * Math.PI * ( offset + 0.33 ) ) ) );
				int blue = ( int )( 255.0 * ( 0.5 + 0.5 * Math.Cos( 2.0 * Math.PI * ( offset + 0.67 ) ) ) );

				mandelbrot_palette[palette_index] = Color.FromArgb( blue, green, red );
			}

			for( int index = 0; index < WindowWidth * WindowHeight; index++ )
			{
				int palette_index = PalettedMandelbrotImage[index];
				mandelbrot_image[index] = mandelbrot_palette[palette_index].ToArgb();
			}

			Bitmap new_bitmap = new Bitmap( WindowWidth, WindowHeight );

			Rectangle lock_area = new Rectangle( 0, 0, new_bitmap.Width, new_bitmap.Height );
			BitmapData bitmap_data = new_bitmap.LockBits( lock_area, ImageLockMode.WriteOnly, new_bitmap.PixelFormat );

			IntPtr start_address = bitmap_data.Scan0;
			Marshal.Copy( mandelbrot_image, 0, start_address, new_bitmap.Width * new_bitmap.Height );

			new_bitmap.UnlockBits( bitmap_data );

			BackgroundImage = new_bitmap;
		}

		// Main program logic
		public SimpleWindow()
		{
			InitializeComponent();

			ClientSize = new Size( WindowWidth, WindowHeight );

			// Recalculate a Mandelbrot set several times to get an average
			for( int iteration = 0; iteration < Iterations; iteration++ )
			{
				Timer.Start();
				CreateMandelbrot();
				Timer.Stop();
			}

			// Draw the resultant bitmap to verify the results
			DrawBitmap();
		}
	}
}
