// Copyright Eternal Developments, LLC. All Rights Reserved.

namespace MandelbrotNet
{
	partial class SimpleWindow

	{
	/// <summary>
	///  Required designer variable.
	/// </summary>
	private System.ComponentModel.IContainer components = null;

	/// <summary>
	///  Clean up any resources being used.
	/// </summary>
	/// <param name="disposing">true if managed resources should be disposed; otherwise, false.</param>
	protected override void Dispose( bool disposing )
	{
		if( disposing && ( components != null ) )
		{
			components.Dispose();
		}

		base.Dispose( disposing );
	}

	/// <summary>
	///  Required method for Designer support - do not modify
	///  the contents of this method with the code editor.
	/// </summary>
	private void InitializeComponent()
	{
		this.SuspendLayout();
		// 
		// SimpleWindow
		// 
		this.AutoScaleDimensions = new System.Drawing.SizeF( 6F, 13F );
		this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
		this.ClientSize = new System.Drawing.Size( 2048, 2048 );
		this.FormBorderStyle = System.Windows.Forms.FormBorderStyle.None;
		this.Name = "SimpleWindow";
		this.Text = "SimpleWindow";
		this.FormClosing += new System.Windows.Forms.FormClosingEventHandler( this.OnFormClosed );
		this.MouseClick += new System.Windows.Forms.MouseEventHandler( this.OnMouseClick );
		this.ResumeLayout( false );
		}
	}
}
