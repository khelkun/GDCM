/*=========================================================================

  Program: GDCM (Grass Root DICOM). A DICOM library
  Module:  $URL$

  Copyright (c) 2006-2008 Mathieu Malaterre
  All rights reserved.
  See Copyright.txt or http://gdcm.sourceforge.net/Copyright.html for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
#include "vtkGDCMThreadedImageReader2.h"

#include "vtkObjectFactory.h"
#include "vtkImageData.h"
#include "vtkStringArray.h"
#include "vtkInformationVector.h"
#include "vtkInformation.h"
#include "vtkDemandDrivenPipeline.h"
#include "vtkStreamingDemandDrivenPipeline.h"

#include "gdcmImageReader.h"

#include <assert.h>

vtkCxxRevisionMacro(vtkGDCMThreadedImageReader2, "$Revision: 1.1 $")
vtkStandardNewMacro(vtkGDCMThreadedImageReader2)
vtkCxxSetObjectMacro(vtkGDCMThreadedImageReader2,FileNames,vtkStringArray)

// Output Ports are as follow:
// #0: The image/volume (root PixelData element)
// #1: (if present): the Icon Image (0088,0200)
// #2-xx: (if present): the Overlay (60xx,3000)

#define IconImagePortNumber 1
#define OverlayPortNumber   2

vtkGDCMThreadedImageReader2::vtkGDCMThreadedImageReader2()
{
  this->SetNumberOfInputPorts(0);
  this->FileLowerLeft = 1;
  this->FileName = NULL;
  this->FileNames = vtkStringArray::New();
  this->LoadIconImage = 0;
  memset(this->DataExtent,0,6*sizeof(*DataExtent));
  this->LoadOverlays = 0;
  this->NumberOfOverlays = 0;
  this->DataScalarType = VTK_VOID;

  this->NumberOfScalarComponents = 1;
  this->DataSpacing[0] = DataSpacing[1] = DataSpacing[2] = 1;
  this->DataOrigin[0] = DataOrigin[1] = DataOrigin[2] = 0;
  memset(this->IconImageDataExtent,0,6*sizeof(*IconImageDataExtent));
}

vtkGDCMThreadedImageReader2::~vtkGDCMThreadedImageReader2()
{
  if( this->FileNames )
    {
    this->FileNames->Delete();
    }
  this->SetFileName(NULL);
}

const char *vtkGDCMThreadedImageReader2::GetFileName(int i)
{
  return this->FileNames->GetValue( i );
}

void vtkGDCMThreadedImageReader2::SetFileName(const char *filename)
{
  if( !filename )
    {
    return;
    }
  //this->FileNames->Clear();
  this->FileNames->InsertNextValue( filename );
}

//----------------------------------------------------------------------------
// Description:
// This templated function executes the filter for any type of data.
template <class T>
void vtkGDCMThreadedImageReader2Execute(vtkGDCMThreadedImageReader2 *self,
                          vtkImageData **inDatas, int numFiles, vtkImageData *outData,
                          int outExt[6], int id, T*)
{
  (void)numFiles; (void)inDatas;
  printf("outExt:%d,%d,%d,%d,%d,%d\n",
    outExt[0], outExt[1], outExt[2], outExt[3], outExt[4], outExt[5]);
  for( int i = outExt[4]; i <= outExt[5]; ++i )
    {
    const char *filename = self->GetFileNames()->GetValue( i );
    //ReadOneFile( filename );
    //outData->GetPointData()->GetScalars()->SetName("GDCMImage");


    if( id == 0 )
      {
      // we only consider outExt here for computing the progress, while in fact we should really
      // consider numFiles to compute exact update progress...oh well let's assume this is almost 
      // correct.
      self->UpdateProgress(float(i)/float(outExt[5]-outExt[4]+1));
      }


    //char * pointer = static_cast<char*>(outData->GetScalarPointerForExtent(outExt));
    char * pointer = static_cast<char*>(outData->GetScalarPointer(0,0,i));
//printf("pointer:%i\n",*pointer);
    gdcm::ImageReader reader;
    reader.SetFileName( filename );
    if( !reader.Read() )
      {
      abort();
      }
    const gdcm::Image &image = reader.GetImage();
    unsigned long len = image.GetBufferLength();
    image.GetBuffer(pointer);
(void)len;
    }

}

int vtkGDCMThreadedImageReader2::RequestInformation (
  vtkInformation * request,
  vtkInformationVector** inputVector,
  vtkInformationVector *outputVector)
{
  (void)request;(void)inputVector;(void)outputVector;
  // Some information need to have been set outside (user specified)
  //assert( this->GetOutput(0)->GetNumberOfPoints() != 0 );
  // For now only handles series:
  if( !this->FileNames && !this->FileName )
    {
    return 0;
    }

  // This reader only implement case where image is flipped upside down
  if( !this->FileLowerLeft )
    {
    vtkErrorMacro( "You need to set the FileLowerLeft flag to On" );
    return 0;
    }

  if( this->FileNames )
    {
    int zmin = 0;
    int zmax = 0;
    zmax = this->FileNames->GetNumberOfValues() - 1;
    if( this->DataExtent[4] != zmin || this->DataExtent[5] != zmax )
      {
      vtkErrorMacro( "Problem with extent" );
      return 0;
      }
    }
  // Cannot deduce anything else otherwise...

  int numvol = 1;
  if( this->LoadIconImage )
    {
    numvol = 2;
    return 0;
    }
  if( this->LoadOverlays )
    {
    this->NumberOfOverlays = 1;
    numvol = 3;
    }
  assert( numvol == 1 || numvol == 3 );
  this->SetNumberOfOutputPorts(numvol);
  assert( this->DataScalarType != VTK_VOID );
  // For each output:
  for(int i = 0; i < numvol; ++i)
    {
    // Allocate !
    if( !this->GetOutput(i) )
      {
      vtkImageData *img = vtkImageData::New();
      this->GetExecutive()->SetOutputData(i, img );
      img->Delete();
      }
    vtkInformation *outInfo = outputVector->GetInformationObject(i);
    switch(i)
      {
    // root Pixel Data
    case 0:
      outInfo->Set(vtkStreamingDemandDrivenPipeline::WHOLE_EXTENT(), this->DataExtent, 6);
      //outInfo->Set(vtkStreamingDemandDrivenPipeline::UPDATE_EXTENT(), this->DataExtent, 6);
      outInfo->Set(vtkDataObject::SPACING(), this->DataSpacing, 3);
      outInfo->Set(vtkDataObject::ORIGIN(), this->DataOrigin, 3);
      vtkDataObject::SetPointDataActiveScalarInfo(outInfo, this->DataScalarType, this->NumberOfScalarComponents);
      break;
    // Icon Image
    case IconImagePortNumber:
      outInfo->Set(vtkStreamingDemandDrivenPipeline::WHOLE_EXTENT(), this->IconImageDataExtent, 6);
      vtkDataObject::SetPointDataActiveScalarInfo(outInfo, VTK_UNSIGNED_CHAR, 1);
      break;
    // Overlays:
    //case OverlayPortNumber:
    default:
      outInfo->Set(vtkStreamingDemandDrivenPipeline::WHOLE_EXTENT(), 
        this->DataExtent[0], this->DataExtent[1], 
        this->DataExtent[2], this->DataExtent[3],
        0,0 );
      vtkDataObject::SetPointDataActiveScalarInfo(outInfo, VTK_UNSIGNED_CHAR, 1);
      break;
      }

    }

  // Ok let's fill in the 'extra' info:
  //FillMedicalImageInformation(reader);

  return 1;
}

// For streaming and threads.  Splits output update extent into num pieces.
// This method needs to be called num times.  Results must not overlap for
// consistent starting extent.  Subclass can override this method.
// This method returns the number of peices resulting from a successful split.
// This can be from 1 to "total".  
// If 1 is returned, the extent cannot be split.
int vtkGDCMThreadedImageReader2::SplitExtent(int splitExt[6], int startExt[6], 
                             int num, int total)
{
  memcpy(splitExt, startExt, 6 * sizeof(*splitExt));

  vtkDebugMacro("SplitExtent: ( " << startExt[0] << ", " << startExt[1] << ", "
                << startExt[2] << ", " << startExt[3] << ", "
                << startExt[4] << ", " << startExt[5] << "), " 
                << num << " of " << total);

  // We should only split along the Z direction (only in the case of multiple files...)
  int splitAxis = 2;
  int min = startExt[4];
  int max = startExt[5];
  if( min >= max )
    {
    assert ( min == 0 );
    assert ( max == 0 );
    return 1;
    }

  // else normal SplitExtent as copied from vtkThreadedImageAlgorithm

  // determine the actual number of pieces that will be generated
  int range = max - min + 1;
  int valuesPerThread = static_cast<int>(ceil(range/static_cast<double>(total)));
  int maxThreadIdUsed = static_cast<int>(ceil(range/static_cast<double>(valuesPerThread))) - 1;
  if (num < maxThreadIdUsed)
    {
    splitExt[splitAxis*2] = splitExt[splitAxis*2] + num*valuesPerThread;
    splitExt[splitAxis*2+1] = splitExt[splitAxis*2] + valuesPerThread - 1;
    }
  if (num == maxThreadIdUsed)
    {
    splitExt[splitAxis*2] = splitExt[splitAxis*2] + num*valuesPerThread;
    }
  
  vtkDebugMacro("  Split Piece: ( " <<splitExt[0]<< ", " <<splitExt[1]<< ", "
                << splitExt[2] << ", " << splitExt[3] << ", "
                << splitExt[4] << ", " << splitExt[5] << ")");

  return maxThreadIdUsed + 1;
}

void vtkGDCMThreadedImageReader2::ThreadedRequestData (
  vtkInformation * vtkNotUsed( request ), 
  vtkInformationVector** vtkNotUsed( inputVector ),
  vtkInformationVector * vtkNotUsed( outputVector ),
  vtkImageData ***inData, 
  vtkImageData **outData,
  int outExt[6], int id)
{
  (void)inData;
  //  printf("ThreadedRequestData::outExt:%d,%d,%d,%d,%d,%d\n",
  //    outExt[0], outExt[1], outExt[2], outExt[3], outExt[4], outExt[5]);

  assert( this->DataScalarType != VTK_VOID );

  switch (this->GetDataScalarType())
    {
    vtkTemplateMacro(
      vtkGDCMThreadedImageReader2Execute(this , 0 , 3,
        outData[0], outExt, id, static_cast<VTK_TT *>(0))
      );
    default:
      vtkErrorMacro(<< "Execute: Unknown ScalarType");
      return;
    }
}

//----------------------------------------------------------------------------
void vtkGDCMThreadedImageReader2::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os,indent);
}

