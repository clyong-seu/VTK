/*=========================================================================

  Program:   Visualization Toolkit
  Module:    vtkUGFacetReader.cxx
  Language:  C++
  Date:      $Date$
  Version:   $Revision$


Copyright (c) 1993-1998 Ken Martin, Will Schroeder, Bill Lorensen.

This software is copyrighted by Ken Martin, Will Schroeder and Bill Lorensen.
The following terms apply to all files associated with the software unless
explicitly disclaimed in individual files. This copyright specifically does
not apply to the related textbook "The Visualization Toolkit" ISBN
013199837-4 published by Prentice Hall which is covered by its own copyright.

The authors hereby grant permission to use, copy, and distribute this
software and its documentation for any purpose, provided that existing
copyright notices are retained in all copies and that this notice is included
verbatim in any distributions. Additionally, the authors grant permission to
modify this software and its documentation for any purpose, provided that
such modifications are not distributed without the explicit consent of the
authors and that existing copyright notices are retained in all copies. Some
of the algorithms implemented by this software are patented, observe all
applicable patent law.

IN NO EVENT SHALL THE AUTHORS OR DISTRIBUTORS BE LIABLE TO ANY PARTY FOR
DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES ARISING OUT
OF THE USE OF THIS SOFTWARE, ITS DOCUMENTATION, OR ANY DERIVATIVES THEREOF,
EVEN IF THE AUTHORS HAVE BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

THE AUTHORS AND DISTRIBUTORS SPECIFICALLY DISCLAIM ANY WARRANTIES, INCLUDING,
BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
PARTICULAR PURPOSE, AND NON-INFRINGEMENT.  THIS SOFTWARE IS PROVIDED ON AN
"AS IS" BASIS, AND THE AUTHORS AND DISTRIBUTORS HAVE NO OBLIGATION TO PROVIDE
MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.


=========================================================================*/
#include "vtkUGFacetReader.h"
#include "vtkByteSwap.h"
#include "vtkMergePoints.h"

// Construct object to extract all parts, and with point merging
// turned on.
vtkUGFacetReader::vtkUGFacetReader()
{
  this->FileName = NULL;
  this->PartColors = NULL;
  this->PartNumber = (-1); //extract all parts

  this->Merging = 1;
  this->Locator = NULL;
}

vtkUGFacetReader::~vtkUGFacetReader()
{
  if ( this->FileName )
    {
    delete [] this->FileName;
    }
  if ( this->PartColors )
    {
    this->PartColors->Delete();
    }
  if (this->Locator != NULL)
    {
    this->Locator->UnRegister(this);
    this->Locator = NULL;
    }
}

// Overload standard modified time function. If locator is modified,
// then this object is modified as well.
unsigned long vtkUGFacetReader::GetMTime()
{
  unsigned long mTime1=this->vtkPolyDataSource::GetMTime();
  unsigned long mTime2;
  
  if (this->Locator)
    {
    mTime2 = this->Locator->GetMTime();
    mTime1 = ( mTime1 > mTime2 ? mTime1 : mTime2 );
    }
  
  return mTime1;
}


void vtkUGFacetReader::Execute()
{
  FILE *fp;
  char header[36];
  struct {float  v1[3], v2[3], v3[3], n1[3], n2[3], n3[3];} facet;
  int ptId[3];
  short ugiiColor, direction;
  int numberTris, numFacetSets, setNumber, facetNumber;
  vtkPoints *newPts, *mergedPts;
  vtkNormals *newNormals, *mergedNormals;
  vtkCellArray *newPolys, *mergedPolys;
  vtkPolyData *output=(vtkPolyData *)this->Output;
  fpos_t pos;
  int triEstimate;

  vtkDebugMacro(<<"Reading UG facet file...");
  if ( this->FileName == NULL )
    {
    vtkErrorMacro(<<"No FileName specified...please specify one.");
    return;
    }

  // open the file
  if ( (fp = fopen(this->FileName, "rb")) == NULL) 
    {
    vtkErrorMacro(<<"Cannot open file specified.");
    return;
    }

  // read the header stuff
  if ( fread (header, 1, 2, fp) <= 0 ||
  fread (&numFacetSets, 4, 1, fp) <= 0 ||
  fread (header, 1, 36, fp) <= 0 )
    {
    vtkErrorMacro(<<"File ended prematurely");
    return;
    }

  // swap bytes since this is a binary file format
  vtkByteSwap::Swap4BE(&numFacetSets);
  
  // Estimate how much space we need - find out the size of the
  // file and divide by 72 bytes per triangle
  fgetpos( fp, &pos );
  fseek( fp, 0L, SEEK_END );
  triEstimate = ftell( fp ) / 72;
  fsetpos( fp, &pos );

  // allocate memory
  if ( ! this->PartColors ) 
    {
    this->PartColors = vtkShortArray::New();
    this->PartColors->Allocate(100);
    }
  else 
    {
    this->PartColors->Reset();
    }

  newPts = vtkPoints::New();
  newPts->Allocate(triEstimate,triEstimate);
  newNormals = vtkNormals::New();
  newNormals->Allocate(triEstimate,triEstimate);
  newPolys = vtkCellArray::New();
  newPolys->Allocate(newPolys->EstimateSize(triEstimate,3),triEstimate);

  // loop over all facet sets, extracting triangles
  for (setNumber=0; setNumber < numFacetSets; setNumber++) 
    {

    if ( fread (&ugiiColor, 2, 1, fp) <= 0 ||
    fread (&direction, 2, 1, fp) <= 0 ||
    fread (&numberTris, 4, 1, fp) <= 0 )
      {
      vtkErrorMacro(<<"File ended prematurely");
      break;
      }

    // swap bytes if necc
    vtkByteSwap::Swap4BE(&numberTris);
    vtkByteSwap::Swap2BERange(&ugiiColor,1);
    vtkByteSwap::Swap2BERange(&direction,1);
    
    this->PartColors->InsertNextValue(ugiiColor);

    for (facetNumber=0; facetNumber < numberTris; facetNumber++)
      {
      if ( fread(&facet,72,1,fp) <= 0 )
        {
        vtkErrorMacro(<<"File ended prematurely");
        break;
        }

      // swap bytes if necc
      vtkByteSwap::Swap4BERange((float *)(&facet),18);
      
      if ( this->PartNumber == -1 || this->PartNumber == setNumber )
        {
        ptId[0] = newPts->InsertNextPoint(facet.v1);
        ptId[1] = newPts->InsertNextPoint(facet.v2);
        ptId[2] = newPts->InsertNextPoint(facet.v3);

        newNormals->InsertNormal(ptId[0],facet.n1);
        newNormals->InsertNormal(ptId[1],facet.n2);
        newNormals->InsertNormal(ptId[2],facet.n3);

        newPolys->InsertNextCell(3,ptId);
        }//if appropriate part
      }//for all facets in this set
    }//for this facet set

  // update output
  vtkDebugMacro(<<"Read " 
                << newPts->GetNumberOfPoints() << " points, "
                << newPolys->GetNumberOfCells() << " triangles.");

  fclose(fp);

  //
  // Merge points/triangles if requested
  //
  if ( this->Merging )
    {
    int npts, *pts, i, nodes[3];
    float *x;

    mergedPts = vtkPoints::New();
    mergedPts->Allocate(newPts->GetNumberOfPoints()/3);
    mergedNormals = vtkNormals::New();
    mergedNormals->Allocate(newNormals->GetNumberOfNormals()/3);
    mergedPolys = vtkCellArray::New();
    mergedPolys->Allocate(newPolys->GetSize());

    if ( this->Locator == NULL )
      {
      this->CreateDefaultLocator();
      }
    this->Locator->InitPointInsertion (mergedPts, newPts->GetBounds());

    for (newPolys->InitTraversal(); newPolys->GetNextCell(npts,pts); )
      {
      for (i=0; i < 3; i++) 
        {
        x = newPts->GetPoint(pts[i]);
        if ( (nodes[i] = this->Locator->IsInsertedPoint(x)) < 0 )
          {
          nodes[i] = this->Locator->InsertNextPoint(x);
          mergedNormals->InsertNormal(nodes[i],newNormals->GetNormal(pts[i]));
          }
        }

      if ( nodes[0] != nodes[1] && nodes[0] != nodes[2] && 
      nodes[1] != nodes[2] )
	{
        mergedPolys->InsertNextCell(3,nodes);
	}
      }

      newPts->Delete();
      newNormals->Delete();
      newPolys->Delete();

      vtkDebugMacro(<< "Merged to: " 
                   << mergedPts->GetNumberOfPoints() << " points, " 
                   << mergedPolys->GetNumberOfCells() << " triangles");
    }
  else
    {
    mergedPts = newPts;
    mergedNormals = newNormals;
    mergedPolys = newPolys;
    }
//
// Update ourselves
//
  output->SetPoints(mergedPts);
  mergedPts->Delete();

  output->GetPointData()->SetNormals(mergedNormals);
  mergedNormals->Delete();

  output->SetPolys(mergedPolys);
  mergedPolys->Delete();

  if (this->Locator)
    {
    this->Locator->Initialize(); //free storage
    }

  output->Squeeze();
}

int vtkUGFacetReader::GetNumberOfParts()
{
  char header[36];
  FILE *fp;
  int numberOfParts;

  if ( this->FileName == NULL )
    {
    vtkErrorMacro(<<"No FileName specified...please specify one.");
    return 0;
    }

  // open the file
  if ( (fp = fopen(this->FileName, "rb")) == NULL) 
    {
    vtkErrorMacro(<<"Cannot open file specified.");
    return 0;
    }

  // read the header stuff
  if ( fread (header, 1, 2, fp) <= 0 ||
  fread (&numberOfParts, 4, 1, fp) <= 0 ||
  fread (header, 1, 36, fp) <= 0 )
    {
    vtkErrorMacro(<<"File ended prematurely");
    fclose(fp);
    return 0;
    }

  // swap bytes if necc
  vtkByteSwap::Swap4BE(&numberOfParts);
  
  fclose(fp);
  return numberOfParts;
}

// Retrieve color index for the parts in the file.
short vtkUGFacetReader::GetPartColorIndex(int partId)
{
  if ( this->PartColors == NULL )
    {
    this->Update();
    }

  if ( !this->PartColors || 
  partId < 0 || partId > this->PartColors->GetMaxId() )
    {
    return 0;
    }
  else
    {
    return this->PartColors->GetValue(partId);
    }
}

// Specify a spatial locator for merging points. By
// default an instance of vtkMergePoints is used.
void vtkUGFacetReader::SetLocator(vtkPointLocator *locator)
{
  if ( this->Locator == locator ) 
    {
    return;
    }
  if (this->Locator != NULL)
    {
    this->Locator->UnRegister(this);
    this->Locator = NULL;
    }
  if (locator != NULL)
    {
    locator->Register(this);
    }
  this->Locator = locator;
  this->Modified();
}

void vtkUGFacetReader::CreateDefaultLocator()
{
  if ( this->Locator == NULL )
    {
    this->Locator = vtkMergePoints::New();
    }
}

void vtkUGFacetReader::PrintSelf(ostream& os, vtkIndent indent)
{
  vtkPolyDataSource::PrintSelf(os,indent);

  os << indent << "File Name: " 
     << (this->FileName ? this->FileName : "(none)") << "\n";

  os << indent << "Part Number: " << this->PartNumber << "\n";

  os << indent << "Merging: " << (this->Merging ? "On\n" : "Off\n");
  if ( this->Locator )
    {
    os << indent << "Locator: " << this->Locator << "\n";
    }
  else
    {
    os << indent << "Locator: (none)\n";
    }
}

