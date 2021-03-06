//-*****************************************************************************
//
// Copyright (c) 2009-2011,
//  Sony Pictures Imageworks, Inc. and
//  Industrial Light & Magic, a division of Lucasfilm Entertainment Company Ltd.
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
// *       Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
// *       Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
// *       Neither the name of Sony Pictures Imageworks, nor
// Industrial Light & Magic nor the names of their contributors may be used
// to endorse or promote products derived from this software without specific
// prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//-*****************************************************************************

#include "IPolyMeshDrw.h"
#include <Alembic/AbcGeom/Visibility.h>
#include "samplingUtils.h"

namespace AlembicHolder {

    static MGLFunctionTable *gGLFT = NULL;

//-*****************************************************************************
IPolyMeshDrw::IPolyMeshDrw( IPolyMesh &iPmesh, std::vector<std::string> path )
  : IObjectDrw( iPmesh, false, path )
  , m_polyMesh( iPmesh )
{
    // Get out if problems.
    if ( !m_polyMesh.valid() )
    {
        return;
    }

    if ( m_polyMesh.getSchema().getNumSamples() > 0 )
    {
        m_polyMesh.getSchema().get( m_samp );
    }

    m_boundsProp = m_polyMesh.getSchema().getSelfBoundsProperty();

    // The object has already set up the min time and max time of
    // all the children.
    // if we have a non-constant time sampling, we should get times
    // out of it.
    TimeSamplingPtr iTsmp = m_polyMesh.getSchema().getTimeSampling();
    if ( !m_polyMesh.getSchema().isConstant() )
    {
        size_t numSamps =  m_polyMesh.getSchema().getNumSamples();
        if ( numSamps > 0 )
        {
            chrono_t minTime = iTsmp->getSampleTime( 0 );
            m_minTime = std::min( m_minTime, minTime );
            chrono_t maxTime = iTsmp->getSampleTime( numSamps-1 );
            m_maxTime = std::max( m_maxTime, maxTime );
        }
    }

	m_currentFrame = MAnimControl::currentTime().value();

    //m_drwHelper.setName(m_object.getFullName());
}

//-*****************************************************************************
IPolyMeshDrw::~IPolyMeshDrw()
{
    // Nothing!
}

//-*****************************************************************************
bool IPolyMeshDrw::valid()
{
    return IObjectDrw::valid() && m_polyMesh.valid();
}

//-*****************************************************************************
void IPolyMeshDrw::setTime( chrono_t iSeconds )
{
	// The frame is different. We should clear all the data.
	if(m_currentFrame != MAnimControl::currentTime().value())
	{
		for (std::map<double, MeshDrwHelper>::iterator iter = m_drwHelpers.begin(); iter != m_drwHelpers.end(); ++iter) 
			iter->second.makeInvalid();

		for (std::map<double, Box3d>::iterator iter = m_bounds.begin(); iter != m_bounds.end(); ++iter) 
			iter->second.makeEmpty();

		m_drwHelpers.clear();
		m_bounds.clear();

		m_currentFrame = MAnimControl::currentTime().value();
	}

	if(m_drwHelpers.count(iSeconds) == 1)
	{
		if (iSeconds != m_currentTime)
			IObjectDrw::setTime( iSeconds );
		return;
	}

    // Use nearest for now.
    Alembic::AbcGeom::IPolyMeshSchema schema = m_polyMesh.getSchema();
	
	if(m_polyMesh.getSchema().getTopologyVariance() == Alembic::AbcGeom::kHeterogenousTopology)
		m_alpha = 0.0f;	
	else
		m_alpha = getWeightAndIndex(iSeconds, schema.getTimeSampling(), schema.getNumSamples(), m_index, m_ceilIndex);

    m_ss =  ISampleSelector(iSeconds, ISampleSelector::kNearIndex );

    if ( IsAncestorInvisible(m_polyMesh,m_ss) ) {
        m_visible = false;
        return;
    }
    else
        m_visible = true;
    if (iSeconds != m_currentTime) {
        IObjectDrw::setTime( iSeconds );
        if ( !valid() )
        {
			m_drwHelpers[iSeconds].makeInvalid();
            return;
        }
        //IPolyMeshSchema::Sample psamp;
        if ( m_polyMesh.getSchema().isConstant() )
        {
			m_drwHelpers[iSeconds].setConstant( m_polyMesh.getSchema().isConstant() );
        }
        else if ( m_polyMesh.getSchema().getNumSamples() > 0 )
        {
            m_polyMesh.getSchema().get( m_samp, m_ss );
            
            IN3fGeomParam normParam = m_polyMesh.getSchema().getNormalsParam();

            if(normParam.valid())
            {
                switch ( normParam.getScope() )
                {
                    case kVaryingScope:
                    case kVertexScope:
                    {
                        m_normal_samp = normParam.getExpandedValue(m_ss);
                        break;
                    }
                    case kFacevaryingScope:
                    {
                        m_normal_samp = normParam.getIndexedValue(m_ss);
                        break;
                    }

                }
                
            }

        }

        m_bounds[iSeconds].makeEmpty();
        m_needtoupdate = true;
    }
}

Box3d IPolyMeshDrw::getBounds()
{
    if(m_bounds[m_currentTime].isEmpty())
	{
		m_ss =  ISampleSelector(m_currentTime, ISampleSelector::kNearIndex );
        m_bounds[m_currentTime] = m_boundsProp.getValue( m_ss );
	}

    return m_bounds[m_currentTime];
}


void IPolyMeshDrw::updateData()
{
    
    Alembic::Abc::P3fArraySamplePtr ceilPoints; 

    if (m_alpha != 0.0) 
    {
			ceilPoints = m_polyMesh.getSchema().getPositionsProperty().getValue( Alembic::Abc::ISampleSelector(m_ceilIndex) ); 
    }

	P3fArraySamplePtr points = m_samp.getPositions();
    Int32ArraySamplePtr indices = m_samp.getFaceIndices();
    Int32ArraySamplePtr counts = m_samp.getFaceCounts();

    N3fArraySamplePtr normals;
    
    if(m_normal_samp.valid())
    {
        
        switch ( m_normal_samp.getScope() )
        {
            case kVaryingScope:
            case kVertexScope:
            {
                normals =  m_normal_samp.getVals();
                break;
            }
            case kFacevaryingScope:
            {
                //unsupported yet.
                break;
            }
        }
    }

    // update the mesh
    m_drwHelpers[m_currentTime].update( points, ceilPoints, normals,
                            indices, counts, getBounds(), m_alpha );

    if ( !m_drwHelpers[m_currentTime].valid() )
    {
        m_polyMesh.reset();
        return;
    }
    m_needtoupdate = false;
}

int IPolyMeshDrw::getNumTriangles()
{
    if ( !valid() )
        return 0;

    return m_drwHelpers[m_currentTime].getNumTriangles();
}

//-*****************************************************************************
void IPolyMeshDrw::draw( const DrawContext &iCtx )
{
    if ( !valid() )
        return;


	holderPrms* params = iCtx.getParams();
	if(!isVisibleForArnold(m_polyMesh, m_currentTime, params, m_visible))
		return;

    if(iCtx.getSelection() != "")
    {
        bool foundInPath = false;

        std::string pathSel = iCtx.getSelection();

        if(!pathInJsonString(m_polyMesh.getFullName(), pathSel))
            return;

    }

    std::map<std::string, MColor> shaderColors = iCtx.getShaderColors();
    MColor objColor(.7, .7,.7, 1.0);

    bool foundInPath = false;
    for(std::map<std::string, MColor>::iterator it = shaderColors.begin(); it != shaderColors.end(); ++it)
    {
        //check both path & tag
        if(it->first.find("/") != std::string::npos)
        {
            if(m_polyMesh.getFullName().find(it->first) != std::string::npos)
            {
                objColor = it->second;
                foundInPath = true;
            }
        }
        else if(matchPattern(m_polyMesh.getFullName(),it->first)) // based on wildcard expression
        {
            objColor = it->second;
            foundInPath = true;
        }
    }
    

    if(shaderColors.size() > 0)
        gGLFT->glColor4f(objColor.r, objColor.g, objColor.b, 1.0f);

    if (m_needtoupdate)
        updateData();


    m_drwHelpers[m_currentTime].draw( iCtx );

    IObjectDrw::draw( iCtx );
}

} // End namespace AlembicHolder
