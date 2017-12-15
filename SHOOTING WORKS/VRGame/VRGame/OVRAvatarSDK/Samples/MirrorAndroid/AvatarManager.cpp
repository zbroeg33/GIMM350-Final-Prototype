/************************************************************************************

Filename    :   AvatarManager.cpp
Content     :   Sample usage of the AvatarSDK.
Created     :
Authors     :

Copyright   :   Copyright 2014 Oculus VR, LLC. All Rights reserved.

*************************************************************************************/

#include "VrCommon.h"
#include "GlBuffer.h"
#include "GlTexture.h"
#include "SurfaceRender.h"
#include "Kernel/OVR_LogUtils.h"

#include "OVR_Avatar.h"
#include "OVR_Platform.h"

#include "AvatarManager.h"

#include <map>

#define MAX_LAYERS 3
#define MAX_JOINTS 17

using namespace OVR;

// Indices for UniformData[]
enum UniformDataIndices
{
	UDI_MATERIAL_PARAMS = 0,
	UDI_MATERIAL_LAYERS = 1,
	UDI_ALPHA_MASK      = 2,
	UDI_LAYER_SURFACE_0 = 3,
	UDI_LAYER_SURFACE_1 = 4,
	UDI_LAYER_SURFACE_2 = 5,
	UDI_JOINT_MATRICES  = 6
};

const char * sVertexShaderSrc =
	"attribute highp vec4 Position;\n"
	"attribute highp vec3 Normal;\n"
	"attribute highp vec2 TexCoord;\n"
	"attribute highp vec4 JointIndices;\n"
	"attribute highp vec4 JointWeights;\n"
	"\n"
	"varying highp vec3 vertexWorldPos;\n"
	"varying highp vec3 vertexViewDir;\n"
	"varying highp vec3 vertexObjPos;\n"
	"varying highp vec3 vertexNormal;\n"
	"varying highp vec2 vertexUV;\n"
	"\n"
	"uniform JointMatrices \n"
	"{\n"
	"	highp mat4 Joints[17];\n"
	"} jb;\n"
	"\n"
	"void main()\n"
	"{\n"
	"	highp vec4 vertexPose;\n"
	"	vertexPose = jb.Joints[ int( JointIndices.x ) ] * Position * JointWeights.x;\n"
	"	vertexPose += jb.Joints[ int( JointIndices.y ) ] * Position * JointWeights.y;\n"
	"	vertexPose += jb.Joints[ int( JointIndices.z ) ] * Position * JointWeights.z;\n"
	"	vertexPose += jb.Joints[ int( JointIndices.w ) ] * Position * JointWeights.w;\n"
	"\n"
	"	highp vec4 normal = vec4( Normal, 0.0 );\n"
	"	highp vec4 normalPose;\n"
	"	normalPose = jb.Joints[ int( JointIndices.x ) ] * normal * JointWeights.x;\n"
	"	normalPose += jb.Joints[ int( JointIndices.y ) ] * normal * JointWeights.y;\n"
	"	normalPose += jb.Joints[ int( JointIndices.z ) ] * normal * JointWeights.z;\n"
	"	normalPose += jb.Joints[ int( JointIndices.w ) ] * normal * JointWeights.w;\n"
	"	normalPose = normalize( normalPose );\n"
	"\n"
	"	gl_Position = TransformVertex( vertexPose );\n"
	"	vertexWorldPos = ( ModelMatrix * vertexPose ).xyz;\n"
	"	vertexViewDir = normalize( -sm.ViewMatrix[ VIEW_ID ][3].xyz - vertexWorldPos );\n"
	"	vertexObjPos = Position.xyz;\n"
	"	vertexNormal = ( ModelMatrix * normalPose ).xyz;\n"
	"	vertexUV = TexCoord;\n"
	"}\n";

const char * sFragmentShaderSrc =
	"#define SAMPLE_MODE_COLOR 0\n"
	"#define SAMPLE_MODE_TEXTURE 1\n"
	"#define SAMPLE_MODE_TEXTURE_SINGLE_CHANNEL 2\n"
	"#define SAMPLE_MODE_PARALLAX 3\n" // unused on Mobile
	"#define SAMPLE_MODE_RSRM 4\n"
	"\n"
	"#define MASK_TYPE_NONE 0\n"
	"#define MASK_TYPE_POSITIONAL 1\n"
	"#define MASK_TYPE_REFLECTION 2\n"
	"#define MASK_TYPE_FRESNEL 3\n"
	"#define MASK_TYPE_PULSE 4\n" // unused on Mobile
	"\n"
	"#define BLEND_MODE_ADD 0\n"
	"#define BLEND_MODE_MULTIPLY 1\n"
	"\n"
	"varying highp vec3 vertexWorldPos;\n"
	"varying highp vec3 vertexViewDir;\n"
	"varying highp vec3 vertexObjPos;\n"
	"varying highp vec3 vertexNormal;\n"
	"varying highp vec2 vertexUV;\n"
	"\n"
	"uniform sampler2D AlphaMask;\n"
	"uniform sampler2D LayerSurface0;\n"
	"uniform sampler2D LayerSurface1;\n"
	"uniform sampler2D LayerSurface2;\n"
	"layout (std140) uniform Parameters\n"
	"{\n"
	"	highp mat4 pProjectorInv;\n"
	"	highp vec4 pBaseColor;\n"
	"	highp vec4 pBaseMaskParameters;\n"
	"	highp vec4 pBaseMaskAxis;\n"
	"	highp vec4 pAlphaMaskScaleOffset;\n"
	"	highp float pElapsedSeconds;\n"
	"	int pBaseMaskType;\n"
	"	int pUseProjector;\n"
	"	int pUseAlpha;\n"
	"} params;\n"
	"uniform LayerState\n"
	"{\n"
	"	int lLayerCount[3];\n"
	"	int lBlendMode[3];\n"
	"	int lSampleMode[3];\n"
	"	int lMaskType[3];\n"
	"	highp vec4 lLayerColor[3];\n"
	"	highp vec4 lSampleParameters[3];\n"
	"	highp vec4 lSampleScaleOffset[3];\n"
	"	highp vec4 lMaskParameters[3];\n"
	"	highp vec4 lMaskAxis[3];\n"
	"} ls;\n"
	"\n"
	"highp vec3 ComputeColor(\n"
	"	int sampleMode, highp vec2 uv, highp vec4 color,\n"
	"	sampler2D surface, highp vec4 surfaceScaleOffset,\n"
	"	highp vec4 sampleParameters, highp vec3 worldNormal )\n"
	"{\n"
	"	if ( sampleMode == SAMPLE_MODE_TEXTURE )\n"
	"	{\n"
	"		highp vec2 panning = params.pElapsedSeconds * sampleParameters.xy;\n"
	"		return texture(\n"
	"			surface,\n"
	"			( uv + panning ) * surfaceScaleOffset.xy + surfaceScaleOffset.zw\n"
	"		).rgb * color.rgb;\n"
	"	}\n"
	"	if ( sampleMode == SAMPLE_MODE_TEXTURE_SINGLE_CHANNEL )\n"
	"	{\n"
	"		highp vec4 channelMask = sampleParameters;\n"
	"		highp vec4 channels =\n"
	"			texture( surface, uv * surfaceScaleOffset.xy + surfaceScaleOffset.zw );\n"
	"		return dot( channelMask, channels ) * color.rgb;\n"
	"	}\n"
	"	else if ( sampleMode == SAMPLE_MODE_RSRM )\n"
	"	{\n"
	"		highp float roughnessMin = sampleParameters.x;\n"
	"		highp vec3 viewReflect = reflect( -vertexViewDir, worldNormal );\n"
	"		highp float viewAngle = viewReflect.y * 0.5 + 0.5;\n"
	"		return texture( surface, vec2( roughnessMin, viewAngle ) ).rgb * color.rgb;\n"
	"	}\n"
	"	return color.rgb;\n"
	"}\n"
	"\n"
	"highp float ComputeMask(\n"
	"	int maskType, highp vec4 maskParameters,\n"
	"	highp vec4 maskAxis, highp vec3 worldNormal )\n"
	"{\n"
	"	if ( maskType == MASK_TYPE_POSITIONAL )\n"
	"	{\n"
	"		highp float centerDistance = maskParameters.x;\n"
	"		highp float fadeAbove = maskParameters.y;\n"
	"		highp float fadeBelow = maskParameters.z;\n"
	"		highp float d = dot( vertexObjPos, maskAxis.xyz );\n"
	"		if ( d > centerDistance )\n"
	"		{\n"
	"			return clamp( 1.0 - ( d - centerDistance ) / fadeAbove, 0.0, 1.0 );\n"
	"		}\n"
	"		else\n"
	"		{\n"
	"			return clamp( 1.0 - ( centerDistance - d ) / fadeBelow, 0.0, 1.0 );\n"
	"		}\n"
	"	}\n"
	"	else if ( maskType == MASK_TYPE_REFLECTION )\n"
	"	{\n"
	"		highp float fadeStart = maskParameters.x;\n"
	"		highp float fadeEnd = maskParameters.y;\n"
	"		highp vec3 viewReflect = reflect( -vertexViewDir, worldNormal );\n"
	"		highp float d = dot( viewReflect, maskAxis.xyz );\n"
	"		return clamp( 1.0 - ( d - fadeStart ) / ( fadeEnd - fadeStart ), 0.0, 1.0 );\n"
	"	}\n"
	"	else if ( maskType == MASK_TYPE_FRESNEL )\n"
	"	{\n"
	"		highp float power = maskParameters.x;\n"
	"		highp float fadeStart = maskParameters.y;\n"
	"		highp float fadeEnd = maskParameters.z;\n"
	"		highp float d = 1.0 - max( 0.0, dot( vertexViewDir, worldNormal ) );\n"
	"		highp float p = pow( d, power );\n"
	"		return clamp( mix( fadeStart, fadeEnd, p ), 0.0, 1.0 );\n"
	"	}\n"
	"	return 1.0;\n"
	"}\n"
	"\n"
	"highp vec3 ComputeBlend( int blendMode, highp vec3 dst, highp vec3 src, highp float mask )\n"
	"{\n"
	"	if ( blendMode == BLEND_MODE_MULTIPLY )\n"
	"	{\n"
	"		return dst * ( src * mask );\n"
	"	}\n"
	"	else\n"
	"	{\n"
	"		return dst + src * mask;\n"
	"	}\n"
	"}\n"
	"\n"
	"void main()\n"
	"{\n"
	"	highp vec2 uv = vertexUV;\n"
	"	if ( params.pUseProjector == 1 )\n"
	"	{\n"
	"		highp vec4 projectorPos = params.pProjectorInv * vec4( vertexWorldPos, 1.0 );\n"
	"		if ( abs( projectorPos.x ) > 1.0 ||\n"
	"			 abs( projectorPos.y ) > 1.0 ||\n"
	"			 abs( projectorPos.z ) > 1.0 )\n"
	"		{\n"
	"			discard;\n"
	"			return;\n"
	"		}\n"
	"		uv = projectorPos.xy * 0.5 + 0.5;\n"
	"	}\n"
	"\n"
	"	highp vec4 color = params.pBaseColor;\n"
	"	highp vec3 worldNormal = normalize( vertexNormal );\n"
	"	for ( int i = 0; i < ls.lLayerCount[0]; ++i )\n"
	"	{\n"
	"		highp vec3 layerColor;\n"
	"		if ( i == 0 )\n"
	"		{\n"
	"			layerColor = ComputeColor(\n"
	"				ls.lSampleMode[i], uv, ls.lLayerColor[i], LayerSurface0,\n"
	"				ls.lSampleScaleOffset[i], ls.lSampleParameters[i], worldNormal );\n"
	"		}\n"
	"		else if ( i == 1 )\n"
	"		{\n"
	"			layerColor = ComputeColor(\n"
	"				ls.lSampleMode[i], uv, ls.lLayerColor[i], LayerSurface1,\n"
	"				ls.lSampleScaleOffset[i], ls.lSampleParameters[i], worldNormal );\n"
	"		}\n"
	"		else\n"
	"		{\n"
	"			layerColor = ComputeColor(\n"
	"				ls.lSampleMode[i], uv, ls.lLayerColor[i], LayerSurface2,\n"
	"				ls.lSampleScaleOffset[i], ls.lSampleParameters[i], worldNormal );\n"
	"		}\n"
	"		highp float layerMask = ComputeMask(\n"
	"			ls.lMaskType[i], ls.lMaskParameters[i], ls.lMaskAxis[i], worldNormal );\n"
	"		color.rgb = ComputeBlend( ls.lBlendMode[i], color.rgb, layerColor, layerMask );\n"
	"	}\n"
	"\n"
	"	if ( params.pUseAlpha == 1 )\n"
	"	{\n"
	"		color.a *= texture(\n"
	"			AlphaMask,\n"
	"			uv * params.pAlphaMaskScaleOffset.xy + params.pAlphaMaskScaleOffset.zw\n"
	"		).r;\n"
	"	}\n"
	"\n"
	"	color.a *= ComputeMask(\n"
	"		params.pBaseMaskType, params.pBaseMaskParameters,\n"
	"		params.pBaseMaskAxis, worldNormal );\n"
	"	gl_FragColor = color;\n"
	"}\n";

static ovrAvatar * sAvatar;
static ovrMicrophone * sMic;
static int sLoadingAssets = 0;
static uint64_t sTestUserID = 0;
static float sElapsedSeconds = 0.0f;
static std::map< ovrAvatarAssetID, GlTexture > sTextures;
static std::map< ovrAvatarAssetID, ovrSurfaceDef > sSurfaceDefs;
static std::map< ovrAvatarAssetID, ovrSurfaceDef > sProjectorSurfaceDefs;
static std::map< ovrAvatarAssetID, Matrix4f * > sInverseBindPoses;

static GlProgram sSkinnedMeshProgram;

static Matrix4f MatrixFromAvatarTransform( const ovrAvatarTransform & transform )
{
	const ovrAvatarQuatf orientation = transform.orientation;
	return
		Matrix4f::Translation( transform.position.x, transform.position.y, transform.position.z ) *
		Matrix4f( Quatf( orientation.x, orientation.y, orientation.z, orientation.w ) ) *
		Matrix4f::Scaling( transform.scale.x, transform.scale.y, transform.scale.z );
}

static int ComputeWorldPose( const ovrAvatarSkinnedMeshPose & localPose, Matrix4f * worldPose )
{
	const int maxJoints = localPose.jointCount < MAX_JOINTS ? localPose.jointCount : MAX_JOINTS;
	for ( int i = 0; i < maxJoints; i++ )
	{
		Matrix4f local = MatrixFromAvatarTransform( localPose.jointTransform[ i ] );
		const int parentIndex = localPose.jointParents[ i ];
		if ( parentIndex < 0 )
		{
			worldPose[ i ] = local;
		}
		else
		{
			worldPose[ i ] = worldPose[ parentIndex ] * local;
		}
	}
	return maxJoints;
}

static void LoadMesh( const ovrAvatarMessage_AssetLoaded * message )
{
	if ( sSurfaceDefs.find( message->assetID ) != sSurfaceDefs.end() )
	{
		return;
	}

	const ovrAvatarMeshAssetData * data = ovrAvatarAsset_GetMeshData( message->asset );

	VertexAttribs attribs;
	attribs.position.Resize( data->vertexCount );
	attribs.normal.Resize( data->vertexCount );
	attribs.uv0.Resize( data->vertexCount );
	attribs.jointIndices.Resize( data->vertexCount );
	attribs.jointWeights.Resize( data->vertexCount );
	for ( uint32_t i = 0; i < data->vertexCount; i++ )
	{
		const ovrAvatarMeshVertex vertex = data->vertexBuffer[i];
		attribs.position[ i ] = Vector3f( vertex.x, vertex.y, vertex.z );
		attribs.normal[ i ] = Vector3f( vertex.nx, vertex.ny, vertex.nz );
		attribs.uv0[ i ] = Vector2f( vertex.u, vertex.v );
		attribs.jointIndices[ i ] = Vector4i(
			vertex.blendIndices[0], vertex.blendIndices[1],
			vertex.blendIndices[2], vertex.blendIndices[3] );
		attribs.jointWeights[ i ] = Vector4f(
			vertex.blendWeights[0], vertex.blendWeights[1],
			vertex.blendWeights[2], vertex.blendWeights[3] );
	}

	Array< TriangleIndex > indices;
	indices.Resize( data->indexCount );
	for ( uint32_t i = 0; i < data->indexCount; i++ )
	{
		indices[ i ] = data->indexBuffer[ i ];
	}

	const ovrAvatarSkinnedMeshPose & meshPose = data->skinnedBindPose;
	Matrix4f * inverseBindPoses =
		( Matrix4f * )malloc( meshPose.jointCount * sizeof( Matrix4f ) );
	int numJoints = ComputeWorldPose( meshPose, inverseBindPoses );
	for ( int i = 0; i < numJoints; i++ )
	{
		inverseBindPoses[ i ].Invert();
	}
	sInverseBindPoses[ message->assetID ] = inverseBindPoses;

	const int sizeParams =
		16 * sizeof( float ) + 4 * sizeof( ovrAvatarVector4f ) + sizeof( float ) + 3 * sizeof( int );
	const int sizeLayers = MAX_LAYERS * ( 4 * sizeof( int ) + 5 * sizeof( ovrAvatarVector4f ) );
	const int sizeJoints = MAX_JOINTS * sizeof( Matrix4f );
	GlBuffer * materialParamsUniformBuffer = new GlBuffer();
	materialParamsUniformBuffer->Create( GLBUFFER_TYPE_UNIFORM, sizeParams, NULL );
	GlBuffer * materialLayersUniformBuffer = new GlBuffer();
	materialLayersUniformBuffer->Create( GLBUFFER_TYPE_UNIFORM, sizeLayers, NULL );
	GlBuffer * jointsUniformBuffer = new GlBuffer();
	jointsUniformBuffer->Create( GLBUFFER_TYPE_UNIFORM, sizeJoints, NULL );

	ovrSurfaceDef surf;
	surf.geo.Create( attribs, indices );
	surf.geo.primitiveType = GL_TRIANGLES;
	surf.graphicsCommand.Program = sSkinnedMeshProgram;
	surf.graphicsCommand.GpuState.blendEnable = ovrGpuState::BLEND_ENABLE_SEPARATE;
	surf.graphicsCommand.GpuState.blendSrc = GL_SRC_ALPHA;
	surf.graphicsCommand.GpuState.blendDst = GL_ONE_MINUS_SRC_ALPHA;
	surf.graphicsCommand.GpuState.blendSrcAlpha = GL_ONE;
	surf.graphicsCommand.GpuState.blendDstAlpha = GL_ONE_MINUS_SRC_ALPHA;
	//surf.graphicsCommand.GpuState.depthMaskEnable = false;
	//surf.graphicsCommand.GpuState.cullEnable = false;
	//surf.graphicsCommand.GpuState.depthEnable = true;
	surf.graphicsCommand.UniformData[ UDI_MATERIAL_PARAMS ].Data =
		(void *)materialParamsUniformBuffer;
	surf.graphicsCommand.UniformData[ UDI_MATERIAL_LAYERS ].Data =
		(void *)materialLayersUniformBuffer;
	surf.graphicsCommand.UniformData[ UDI_JOINT_MATRICES ].Data = (void *)jointsUniformBuffer;

	sSurfaceDefs[ message->assetID ] = surf;

	// Projector surface
	materialParamsUniformBuffer = new GlBuffer();
	materialParamsUniformBuffer->Create( GLBUFFER_TYPE_UNIFORM, sizeParams, NULL );
	materialLayersUniformBuffer = new GlBuffer();
	materialLayersUniformBuffer->Create( GLBUFFER_TYPE_UNIFORM, sizeLayers, NULL );
	jointsUniformBuffer = new GlBuffer();
	jointsUniformBuffer->Create( GLBUFFER_TYPE_UNIFORM, sizeJoints, NULL );

	ovrSurfaceDef projectorSurf = surf;
	projectorSurf.graphicsCommand.GpuState.depthFunc = GL_EQUAL;
	projectorSurf.graphicsCommand.GpuState.depthMaskEnable = false;
	projectorSurf.graphicsCommand.UniformData[ UDI_MATERIAL_PARAMS ].Data =
		(void *)materialParamsUniformBuffer;
	projectorSurf.graphicsCommand.UniformData[ UDI_MATERIAL_LAYERS ].Data =
		(void *)materialLayersUniformBuffer;
	projectorSurf.graphicsCommand.UniformData[ UDI_JOINT_MATRICES ].Data =
		(void *)jointsUniformBuffer;

	sProjectorSurfaceDefs[ message->assetID ] = projectorSurf;
}

static void LoadTexture( const ovrAvatarMessage_AssetLoaded * message )
{
	if ( sTextures.find( message->assetID ) != sTextures.end() )
	{
		return;
	}

	const ovrAvatarTextureAssetData * data = ovrAvatarAsset_GetTextureData( message->asset );

	if ( data->format == ovrAvatarTextureFormat_RGB24 )
	{
		GlTexture texture =
			LoadRGBTextureFromMemory( data->textureData, data->sizeX, data->sizeY, false );
		sTextures[ message->assetID ] = texture;
	}
	else
	{
		GlTexture texture = LoadASTCTextureFromMemory( data->textureData, data->textureDataSize, 1 );
		sTextures[ message->assetID ] = texture;
	}
}

static void SetSurfaceState(
	ovrSurfaceDef & surface,
	const ovrAvatarRenderPart_SkinnedMeshRender * mesh,
	const ovrAvatarMaterialState & materialState,
	const Matrix4f * const projectorInv )
{
	const int sizeInt = sizeof( int );
	const int sizeFloat = sizeof( float );
	const int sizeVec4 = sizeof( ovrAvatarVector4f );

	// Material base params
	GlBuffer * buffer = (GlBuffer *)surface.graphicsCommand.UniformData[ UDI_MATERIAL_PARAMS ].Data;
	char * pBuffer = (char *)buffer->MapBuffer();

	Matrix4f projectorInvMat;
	int useProjector = 0;
	if ( projectorInv )
	{
		useProjector = 1;
		projectorInvMat = *projectorInv;
	}
	memcpy( pBuffer, &projectorInvMat.M[0][0], 16 * sizeFloat );

	memcpy( pBuffer += 16 * sizeFloat, &materialState.baseColor, sizeVec4 );
	memcpy( pBuffer += sizeVec4, &materialState.baseMaskParameters, sizeVec4 );
	memcpy( pBuffer += sizeVec4, &materialState.baseMaskAxis, sizeVec4 );
	memcpy( pBuffer += sizeVec4, &materialState.alphaMaskScaleOffset, sizeVec4 );
	memcpy( pBuffer += sizeVec4, &sElapsedSeconds, sizeFloat );
	memcpy( pBuffer += sizeFloat, &materialState.baseMaskType, sizeInt );
	memcpy( pBuffer += sizeInt, &useProjector, sizeInt );

	int useAlphaMask = 0;
	if ( materialState.alphaMaskTextureID != 0 )
	{
		useAlphaMask = 1;
		surface.graphicsCommand.UniformData[ UDI_ALPHA_MASK ].Data =
			&sTextures[ materialState.alphaMaskTextureID ];
	}
	memcpy( pBuffer += sizeInt, &useAlphaMask, sizeInt );

	buffer->UnmapBuffer();

	// Material layers
	surface.graphicsCommand.UniformData[ UDI_LAYER_SURFACE_0 ].Data =
		&sTextures[ materialState.layers[0].sampleTexture ];
	surface.graphicsCommand.UniformData[ UDI_LAYER_SURFACE_1 ].Data =
		&sTextures[ materialState.layers[1].sampleTexture ];
	surface.graphicsCommand.UniformData[ UDI_LAYER_SURFACE_2 ].Data =
		&sTextures[ materialState.layers[2].sampleTexture ];

	buffer = (GlBuffer *)surface.graphicsCommand.UniformData[ UDI_MATERIAL_LAYERS ].Data;
	pBuffer = (char *)buffer->MapBuffer();

	int layerCount = materialState.layerCount > MAX_LAYERS ? MAX_LAYERS : materialState.layerCount;
	for ( int l = 0; l < MAX_LAYERS; l++ )
	{
		memcpy( pBuffer, &layerCount, sizeInt );
		pBuffer += sizeInt;
	}

	for ( int l = 0; l < MAX_LAYERS; l++ )
	{
		int idx = l < layerCount ? l : 0;
		memcpy( pBuffer, &materialState.layers[ idx ].blendMode, sizeInt );
		pBuffer += sizeInt;
	}

	for ( int l = 0; l < MAX_LAYERS; l++ )
	{
		int idx = l < layerCount ? l : 0;
		memcpy( pBuffer, &materialState.layers[ idx ].sampleMode, sizeInt );
		pBuffer += sizeInt;
	}

	for ( int l = 0; l < MAX_LAYERS; l++ )
	{
		int idx = l < layerCount ? l : 0;
		memcpy( pBuffer, &materialState.layers[ idx ].maskType, sizeInt );
		pBuffer += sizeInt;
	}

	for ( int l = 0; l < MAX_LAYERS; l++ )
	{
		int idx = l < layerCount ? l : 0;
		memcpy( pBuffer, &materialState.layers[ idx ].layerColor, sizeVec4 );
		pBuffer += sizeVec4;
	}

	for ( int l = 0; l < MAX_LAYERS; l++ )
	{
		int idx = l < layerCount ? l : 0;
		memcpy( pBuffer, &materialState.layers[ idx ].sampleParameters, sizeVec4 );
		pBuffer += sizeVec4;
	}

	for ( int l = 0; l < MAX_LAYERS; l++ )
	{
		int idx = l < layerCount ? l : 0;
		memcpy( pBuffer, &materialState.layers[ idx ].sampleScaleOffset, sizeVec4 );
		pBuffer += sizeVec4;
	}

	for ( int l = 0; l < MAX_LAYERS; l++ )
	{
		int idx = l < layerCount ? l : 0;
		memcpy( pBuffer, &materialState.layers[ idx ].maskParameters, sizeVec4 );
		pBuffer += sizeVec4;
	}

	for ( int l = 0; l < MAX_LAYERS; l++ )
	{
		int idx = l < layerCount ? l : 0;
		memcpy( pBuffer, &materialState.layers[ idx ].maskAxis, sizeVec4 );
		pBuffer += sizeVec4;
	}

	buffer->UnmapBuffer();

	// Joints
	static Matrix4f transposedJoints[ MAX_JOINTS ];
	const int numJoints = ComputeWorldPose( mesh->skinnedPose, transposedJoints );
	for ( int l = 0; l < numJoints; l++ )
	{
		transposedJoints[ l ] =
			transposedJoints[ l ] * sInverseBindPoses[ mesh->meshAssetID ][ l ];
		transposedJoints[ l ] = transposedJoints[ l ].Transposed();
	}
	( (GlBuffer *)surface.graphicsCommand.UniformData[ UDI_JOINT_MATRICES ].Data )->Update(
		MAX_JOINTS * sizeof( Matrix4f ),
		&transposedJoints[ 0 ] );
}

static void HandleAvatarSpecification( const ovrAvatarMessage_AvatarSpecification * message )
{
	// Create the avatar instance
	int capabilities =
		ovrAvatarCapability_Body | ovrAvatarCapability_Voice | ovrAvatarCapability_Base;
	sAvatar = ovrAvatar_Create( message->avatarSpec, (ovrAvatarCapabilities)capabilities );

	// Trigger load operations for all of the assets referenced by the avatar
	uint32_t refCount = ovrAvatar_GetReferencedAssetCount( sAvatar );
	for ( uint32_t i = 0; i < refCount; ++i )
	{
		ovrAvatarAssetID id = ovrAvatar_GetReferencedAsset( sAvatar, i );
		ovrAvatarAsset_BeginLoading( id );
		sLoadingAssets++;
	}
}

static void HandleAvatarAssetLoaded( const ovrAvatarMessage_AssetLoaded * message )
{
	// Determine the type of the asset that got loaded
	ovrAvatarAssetType assetType = ovrAvatarAsset_GetType( message->asset );

	// Call the appropriate loader function
	if ( assetType == ovrAvatarAssetType_Mesh )
	{
		LoadMesh( message );
	}
	else if ( assetType == ovrAvatarAssetType_Texture )
	{
		LoadTexture( message );
	}

	sLoadingAssets--;
}

static void HandleAvatarSDKMessages()
{
	ovrAvatarMessage * message = nullptr;
	while ( ( message = ovrAvatarMessage_Pop() ) != nullptr )
	{
		ovrAvatarMessageType type = ovrAvatarMessage_GetType( message );
		switch ( type )
		{
			case ovrAvatarMessageType_AvatarSpecification:
				HandleAvatarSpecification( ovrAvatarMessage_GetAvatarSpecification( message ) );
				break;
			case ovrAvatarMessageType_AssetLoaded:
				HandleAvatarAssetLoaded( ovrAvatarMessage_GetAssetLoaded( message ) );
				break;
			default:
				break;
		}
		ovrAvatarMessage_Free( message );
	}
}

AvatarManager::AvatarManager()
{
}

AvatarManager::~AvatarManager()
{
}

void AvatarManager::Initialize( const char* appId, jobject activity, JNIEnv* jni )
{
	// Create the microphone for voice effects
	sMic = ovr_Microphone_Create();
	if ( sMic )
	{
		ovr_Microphone_Start( sMic );
	}

	// Shaders
	static ovrProgramParm uniformParms[] = {
		{ "Parameters", ovrProgramParmType::BUFFER_UNIFORM },
		{ "LayerState", ovrProgramParmType::BUFFER_UNIFORM },
		{ "AlphaMask", ovrProgramParmType::TEXTURE_SAMPLED },
		{ "LayerSurface0", ovrProgramParmType::TEXTURE_SAMPLED },
		{ "LayerSurface1", ovrProgramParmType::TEXTURE_SAMPLED },
		{ "LayerSurface2", ovrProgramParmType::TEXTURE_SAMPLED },
		{ "JointMatrices", ovrProgramParmType::BUFFER_UNIFORM },
	};
	sSkinnedMeshProgram = GlProgram::Build(
		sVertexShaderSrc,
		sFragmentShaderSrc,
		uniformParms,
		sizeof( uniformParms ) / sizeof( ovrProgramParm )
	);

	sTestUserID = ovr_GetLoggedInUserID();
	ovrAvatar_InitializeAndroid( appId, activity, jni );
	ovrAvatar_RequestAvatarSpecification( sTestUserID );
}

void AvatarManager::Destroy()
{
	typedef std::map< ovrAvatarAssetID, GlTexture >::iterator it_type2;
	for ( it_type2 iterator = sTextures.begin(); iterator != sTextures.end(); iterator++ )
	{
		DeleteTexture( iterator->second );
	}
	sTextures.clear();

	typedef std::map< ovrAvatarAssetID, ovrSurfaceDef >::iterator it_type3;
	for ( it_type3 iterator = sSurfaceDefs.begin(); iterator != sSurfaceDefs.end(); iterator++ )
	{
		iterator->second.geo.Free();

		GlBuffer * buffer =
			(GlBuffer *)iterator->second.graphicsCommand.UniformData[ UDI_MATERIAL_PARAMS ].Data;
		buffer->Destroy();
		delete buffer;

		buffer =
			(GlBuffer *)iterator->second.graphicsCommand.UniformData[ UDI_MATERIAL_LAYERS ].Data;
		buffer->Destroy();
		delete buffer;

		buffer =
			(GlBuffer *)iterator->second.graphicsCommand.UniformData[ UDI_JOINT_MATRICES ].Data;
		buffer->Destroy();
		delete buffer;
	}
	sSurfaceDefs.clear();

	for ( it_type3 iterator = sProjectorSurfaceDefs.begin(); iterator != sProjectorSurfaceDefs.end(); iterator++ )
	{
		// Geometry was shared with sSurfaceDefs, so no need to free it again

		GlBuffer * buffer =
			(GlBuffer *)iterator->second.graphicsCommand.UniformData[ UDI_MATERIAL_PARAMS ].Data;
		buffer->Destroy();
		delete buffer;

		buffer =
			(GlBuffer *)iterator->second.graphicsCommand.UniformData[ UDI_MATERIAL_LAYERS ].Data;
		buffer->Destroy();
		delete buffer;

		buffer =
			(GlBuffer *)iterator->second.graphicsCommand.UniformData[ UDI_JOINT_MATRICES ].Data;
		buffer->Destroy();
		delete buffer;
	}
	sProjectorSurfaceDefs.clear();

	typedef std::map< ovrAvatarAssetID, Matrix4f * >::iterator it_type4;
	for ( it_type4 iterator = sInverseBindPoses.begin(); iterator != sInverseBindPoses.end(); iterator++ )
	{
		free( iterator->second );
	}
	sInverseBindPoses.clear();

	GlProgram::Free( sSkinnedMeshProgram );

	ovrAvatar_Destroy( sAvatar );
	ovrAvatar_Shutdown();

	if ( sMic )
	{
		ovr_Microphone_Destroy( sMic );
		sMic = NULL;
	}
}

void AvatarManager::Frame( const ovrFrameInput & vrFrame, const Matrix4f viewMatrix )
{
	sElapsedSeconds += vrFrame.DeltaSeconds;

	// Swipe left/right to change test material
	const ovrID userID = ovr_GetLoggedInUserID();
	const bool isSwipeBack = vrFrame.Input.buttonPressed & BUTTON_SWIPE_BACK;
	const bool isSwipeForward = vrFrame.Input.buttonPressed & BUTTON_SWIPE_FORWARD;
	if ( isSwipeForward || ( sTestUserID != userID && isSwipeBack ) )
	{
		if ( userID != 0 && sTestUserID == userID )
		{
			sTestUserID = 0;
		}
		else if ( userID != 0 && sTestUserID == 0 && isSwipeBack )
		{
			sTestUserID = userID;
		}
		else
		{
			sTestUserID += isSwipeBack ? -1 : 1;
		}

		ovrAvatar_Destroy( sAvatar );
		sAvatar = NULL;
		ovrAvatar_RequestAvatarSpecification( sTestUserID );
	}

	// Handle new SDK messages
	HandleAvatarSDKMessages();

	// Update Avatar
	if ( sAvatar == NULL || sLoadingAssets > 0 )
	{
		return;
	}

	// Update the avatar pose from the inputs
	ovrAvatarTransform pose;
	const Vector3f position = GetViewMatrixPosition( viewMatrix );
	pose.position.x = position.x;
	pose.position.y = position.y;
	pose.position.z = position.z;
	pose.orientation.x = vrFrame.Tracking.HeadPose.Pose.Orientation.x;
	pose.orientation.y = vrFrame.Tracking.HeadPose.Pose.Orientation.y;
	pose.orientation.z = vrFrame.Tracking.HeadPose.Pose.Orientation.z;
	pose.orientation.w = vrFrame.Tracking.HeadPose.Pose.Orientation.w;
	pose.scale.x = pose.scale.y = pose.scale.z = 1.0f;
	ovrAvatarPose_UpdateBody( sAvatar, pose );

	// If we have a mic, update the voice visualization
	if ( sMic )
	{
		float samples[48000];
		size_t sampleCount =
			ovr_Microphone_ReadData( sMic, samples, sizeof( samples ) / sizeof( samples[0] ) );
		if ( sampleCount > 0 )
		{
			ovrAvatarPose_UpdateVoiceVisualization( sAvatar, (uint32_t)sampleCount, samples );
		}
	}

	ovrAvatarPose_Finalize( sAvatar, vrFrame.DeltaSeconds );
}

void AvatarManager::AppendSurfaceList( Array< ovrDrawSurface > * surfaceList )
{
	if ( sAvatar == NULL || sLoadingAssets > 0 )
	{
		return;
	}

	// Render avatar
	uint32_t componentCount = ovrAvatarComponent_Count( sAvatar );
	for ( uint32_t i = 0; i < componentCount; i++ )
	{
		const ovrAvatarComponent * component = ovrAvatarComponent_Get( sAvatar, i );

		for ( uint32_t j = 0; j < component->renderPartCount; j++ )
		{
			const ovrAvatarRenderPart * renderPart = component->renderParts[j];
			ovrAvatarRenderPartType type = ovrAvatarRenderPart_GetType( renderPart );
			if ( type == ovrAvatarRenderPartType_SkinnedMeshRender )
			{
				const ovrAvatarRenderPart_SkinnedMeshRender * mesh =
					ovrAvatarRenderPart_GetSkinnedMeshRender( renderPart );

				if ( !( mesh->visibilityMask & ovrAvatarVisibilityFlag_ThirdPerson ) )
				{
					continue;
				}

				ovrSurfaceDef & surface = sSurfaceDefs[ mesh->meshAssetID ];
				SetSurfaceState( surface, mesh, mesh->materialState, NULL );

				Matrix4f modelMatrix =
					Matrix4f::Translation( 0.0f, 0.0f, -1.0f ) *
					Matrix4f::RotationY( Mathf::Pi ) *
					MatrixFromAvatarTransform( component->transform ) *
					MatrixFromAvatarTransform( mesh->localTransform );

				surfaceList->PushBack( ovrDrawSurface( modelMatrix, &surface ) );
			}
			else if ( type == ovrAvatarRenderPartType_ProjectorRender )
			{
				const ovrAvatarRenderPart_ProjectorRender * projector =
					ovrAvatarRenderPart_GetProjectorRender( renderPart );

				// Retrieve the mesh transform
				const ovrAvatarComponent * comp =
					ovrAvatarComponent_Get( sAvatar, projector->componentIndex );
				const ovrAvatarRenderPart * part =
					comp->renderParts[ projector->renderPartIndex ];
				const ovrAvatarRenderPart_SkinnedMeshRender * mesh =
					ovrAvatarRenderPart_GetSkinnedMeshRender( part );

				// Compute the projection matrix
				Matrix4f projection =
					Matrix4f::Translation( 0.0f, 0.0f, -1.0f ) *
					Matrix4f::RotationY( Mathf::Pi ) *
					MatrixFromAvatarTransform( component->transform ) *
					MatrixFromAvatarTransform( projector->localTransform );
				Matrix4f projectionInv = projection.Inverted().Transposed();

				// Update surface state
				ovrSurfaceDef & surface = sProjectorSurfaceDefs[ mesh->meshAssetID ];
				SetSurfaceState( surface, mesh, projector->materialState, &projectionInv );

				// Compute the mesh transform
				Matrix4f modelMatrix =
					Matrix4f::Translation( 0.0f, 0.0f, -1.0f ) *
					Matrix4f::RotationY( Mathf::Pi ) *
					MatrixFromAvatarTransform( comp->transform ) *
					MatrixFromAvatarTransform( mesh->localTransform );

				surfaceList->PushBack( ovrDrawSurface( modelMatrix, &surface ) );
			}
		}
	}
}
