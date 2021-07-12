// CompoundDocument:1
#include "xleres/TechniqueLibrary/System/Prefix.hlsl"

#include "xleres/Nodes/Texture.sh"
#include "xleres/ToolsHelper/DecodeParametersTexture_ColoredSpecular.sh"
#include "xleres/Nodes/Basic.sh"
#include "xleres/Nodes/MaterialParam.sh"
#include "xleres/Nodes/Output.sh"


Texture2D Mask;
Texture2D Diffuse;
Texture2D Specular;
cbuffer BasicMaterialConstants
{
	float2 BRoughnessRange;
	float2 BSpecularRange;
	float2 BMetalRange;
	float2 ARoughnessRange;
	float2 ASpecularRange;
	float2 AMetalRange;
	float2 GRoughnessRange;
	float2 GSpecularRange;
	float2 GMetalRange;
	float2 RRoughnessRange;
	float2 RSpecularRange;
	float2 RMetalRange;
}
void SpecularWithMask(uint2 pixelCoords, float2 texCoord : TEXCOORD0, out float4 paramTex : SV_Target0)
{
	float4 Output_24_result;
	Output_24_result = LoadAbsolute( Mask, pixelCoords );

	float4 Output_4_result;
	Output_4_result = Sample( Specular, texCoord );

	float3 Output_5_rgb;
	float Output_5_alpha;
	SeparateAlpha( Output_4_result, Output_5_rgb, Output_5_alpha );

	float4 Output_3_result;
	Output_3_result = Sample( Diffuse, texCoord );

	float3 Output_6_rgb;
	float Output_6_alpha;
	SeparateAlpha( Output_3_result, Output_6_rgb, Output_6_alpha );

	float3 Output_2_finalDiffuseSample;
	CommonMaterialParam Output_2_materialParam;
	DecodeParametersTexture_ColoredSpecular( RMetalRange, RRoughnessRange, RSpecularRange, Output_6_rgb, Output_5_rgb, Output_2_finalDiffuseSample, Output_2_materialParam );

	float Output_22_r;
	float Output_22_g;
	float Output_22_b;
	float Output_22_a;
	Separate4( Output_24_result, Output_22_r, Output_22_g, Output_22_b, Output_22_a );

	float Output_23_result;
	Output_23_result = Remap1( Output_22_a, float2(0,1), float2(1,0) );

	float4 Output_10_result;
	Combine4( Output_22_r, Output_22_g, Output_22_b, Output_23_result, Output_10_result );

	float Output_11_result;
	Output_11_result = AddMany1( Output_22_r, Output_22_g, Output_22_b, Output_23_result );

	float4 Output_9_result;
	Output_9_result = Divide4Scalar( Output_10_result, Output_11_result );

	float Output_12_r;
	float Output_12_g;
	float Output_12_b;
	float Output_12_a;
	Separate4( Output_9_result, Output_12_r, Output_12_g, Output_12_b, Output_12_a );

	CommonMaterialParam Output_7_result;
	Output_7_result = Scale( Output_2_materialParam, Output_12_r );

	float3 Output_21_finalDiffuseSample;
	CommonMaterialParam Output_21_materialParam;
	DecodeParametersTexture_ColoredSpecular( GMetalRange, GRoughnessRange, GSpecularRange, Output_6_rgb, Output_5_rgb, Output_21_finalDiffuseSample, Output_21_materialParam );

	CommonMaterialParam Output_18_result;
	Output_18_result = Scale( Output_21_materialParam, Output_12_g );

	CommonMaterialParam Output_8_result;
	Output_8_result = Add( Output_7_result, Output_18_result );

	float3 Output_19_finalDiffuseSample;
	CommonMaterialParam Output_19_materialParam;
	DecodeParametersTexture_ColoredSpecular( AMetalRange, ARoughnessRange, ASpecularRange, Output_6_rgb, Output_5_rgb, Output_19_finalDiffuseSample, Output_19_materialParam );

	CommonMaterialParam Output_15_result;
	Output_15_result = Scale( Output_19_materialParam, Output_12_a );

	float3 Output_20_finalDiffuseSample;
	CommonMaterialParam Output_20_materialParam;
	DecodeParametersTexture_ColoredSpecular( BMetalRange, BRoughnessRange, BSpecularRange, Output_6_rgb, Output_5_rgb, Output_20_finalDiffuseSample, Output_20_materialParam );

	CommonMaterialParam Output_17_result;
	Output_17_result = Scale( Output_20_materialParam, Output_12_b );

	CommonMaterialParam Output_16_result;
	Output_16_result = Add( Output_8_result, Output_17_result );

	CommonMaterialParam Output_14_result;
	Output_14_result = Add( Output_16_result, Output_15_result );

	float4 Output_13_result;
	Output_13_result = Output_ParamTex( Output_14_result );

	paramTex = Output_13_result;

}
/* <<Chunk:NodeGraph:SpecularWithMask>>--(
<?xml version="1.0" encoding="utf-8"?>
<NodeGraph xmlns:i="http://www.w3.org/2001/XMLSchema-instance" xmlns="http://schemas.datacontract.org/2004/07/ShaderPatcherLayer">
	<ConstantConnections>
		<ConstantConnection>
			<OutputNodeID>23</OutputNodeID>
			<OutputParameterName>inputRange</OutputParameterName>
			<Value>float2(0,1)</Value>
		</ConstantConnection>
		<ConstantConnection>
			<OutputNodeID>23</OutputNodeID>
			<OutputParameterName>outputRange</OutputParameterName>
			<Value>float2(1,0)</Value>
		</ConstantConnection>
	</ConstantConnections>
	<InputParameterConnections>
		<InputParameterConnection>
			<OutputNodeID>20</OutputNodeID>
			<OutputParameterName>roughnessRange</OutputParameterName>
			<Default i:nil="true" />
			<Name>BRoughnessRange</Name>
			<Semantic></Semantic>
			<Type>auto</Type>
			<VisualNodeId>1</VisualNodeId>
		</InputParameterConnection>
		<InputParameterConnection>
			<OutputNodeID>20</OutputNodeID>
			<OutputParameterName>specularRange</OutputParameterName>
			<Default i:nil="true" />
			<Name>BSpecularRange</Name>
			<Semantic></Semantic>
			<Type>auto</Type>
			<VisualNodeId>1</VisualNodeId>
		</InputParameterConnection>
		<InputParameterConnection>
			<OutputNodeID>20</OutputNodeID>
			<OutputParameterName>metalRange</OutputParameterName>
			<Default i:nil="true" />
			<Name>BMetalRange</Name>
			<Semantic></Semantic>
			<Type>auto</Type>
			<VisualNodeId>1</VisualNodeId>
		</InputParameterConnection>
		<InputParameterConnection>
			<OutputNodeID>19</OutputNodeID>
			<OutputParameterName>roughnessRange</OutputParameterName>
			<Default i:nil="true" />
			<Name>ARoughnessRange</Name>
			<Semantic></Semantic>
			<Type>auto</Type>
			<VisualNodeId>2</VisualNodeId>
		</InputParameterConnection>
		<InputParameterConnection>
			<OutputNodeID>19</OutputNodeID>
			<OutputParameterName>specularRange</OutputParameterName>
			<Default i:nil="true" />
			<Name>ASpecularRange</Name>
			<Semantic></Semantic>
			<Type>auto</Type>
			<VisualNodeId>2</VisualNodeId>
		</InputParameterConnection>
		<InputParameterConnection>
			<OutputNodeID>19</OutputNodeID>
			<OutputParameterName>metalRange</OutputParameterName>
			<Default i:nil="true" />
			<Name>AMetalRange</Name>
			<Semantic></Semantic>
			<Type>auto</Type>
			<VisualNodeId>2</VisualNodeId>
		</InputParameterConnection>
		<InputParameterConnection>
			<OutputNodeID>21</OutputNodeID>
			<OutputParameterName>roughnessRange</OutputParameterName>
			<Default i:nil="true" />
			<Name>GRoughnessRange</Name>
			<Semantic></Semantic>
			<Type>auto</Type>
			<VisualNodeId>3</VisualNodeId>
		</InputParameterConnection>
		<InputParameterConnection>
			<OutputNodeID>21</OutputNodeID>
			<OutputParameterName>specularRange</OutputParameterName>
			<Default i:nil="true" />
			<Name>GSpecularRange</Name>
			<Semantic></Semantic>
			<Type>auto</Type>
			<VisualNodeId>3</VisualNodeId>
		</InputParameterConnection>
		<InputParameterConnection>
			<OutputNodeID>21</OutputNodeID>
			<OutputParameterName>metalRange</OutputParameterName>
			<Default i:nil="true" />
			<Name>GMetalRange</Name>
			<Semantic></Semantic>
			<Type>auto</Type>
			<VisualNodeId>3</VisualNodeId>
		</InputParameterConnection>
		<InputParameterConnection>
			<OutputNodeID>2</OutputNodeID>
			<OutputParameterName>roughnessRange</OutputParameterName>
			<Default i:nil="true" />
			<Name>RRoughnessRange</Name>
			<Semantic></Semantic>
			<Type>auto</Type>
			<VisualNodeId>4</VisualNodeId>
		</InputParameterConnection>
		<InputParameterConnection>
			<OutputNodeID>2</OutputNodeID>
			<OutputParameterName>specularRange</OutputParameterName>
			<Default i:nil="true" />
			<Name>RSpecularRange</Name>
			<Semantic></Semantic>
			<Type>auto</Type>
			<VisualNodeId>4</VisualNodeId>
		</InputParameterConnection>
		<InputParameterConnection>
			<OutputNodeID>2</OutputNodeID>
			<OutputParameterName>metalRange</OutputParameterName>
			<Default i:nil="true" />
			<Name>RMetalRange</Name>
			<Semantic></Semantic>
			<Type>auto</Type>
			<VisualNodeId>4</VisualNodeId>
		</InputParameterConnection>
		<InputParameterConnection>
			<OutputNodeID>24</OutputNodeID>
			<OutputParameterName>inputTexture</OutputParameterName>
			<Default i:nil="true" />
			<Name>Mask</Name>
			<Semantic></Semantic>
			<Type>auto</Type>
			<VisualNodeId>5</VisualNodeId>
		</InputParameterConnection>
		<InputParameterConnection>
			<OutputNodeID>3</OutputNodeID>
			<OutputParameterName>inputTexture</OutputParameterName>
			<Default i:nil="true" />
			<Name>Diffuse</Name>
			<Semantic></Semantic>
			<Type>auto</Type>
			<VisualNodeId>5</VisualNodeId>
		</InputParameterConnection>
		<InputParameterConnection>
			<OutputNodeID>4</OutputNodeID>
			<OutputParameterName>inputTexture</OutputParameterName>
			<Default i:nil="true" />
			<Name>Specular</Name>
			<Semantic></Semantic>
			<Type>auto</Type>
			<VisualNodeId>5</VisualNodeId>
		</InputParameterConnection>
	</InputParameterConnections>
	<NodeConnections>
		<NodeConnection>
			<OutputNodeID>22</OutputNodeID>
			<OutputParameterName>input</OutputParameterName>
			<InputNodeID>24</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>float4</InputType>
			<OutputType>float4</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>7</OutputNodeID>
			<OutputParameterName>input</OutputParameterName>
			<InputNodeID>2</InputNodeID>
			<InputParameterName>materialParam</InputParameterName>
			<InputType>CommonMaterialParam</InputType>
			<OutputType>CommonMaterialParam</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>6</OutputNodeID>
			<OutputParameterName>input</OutputParameterName>
			<InputNodeID>3</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>float4</InputType>
			<OutputType>float4</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>5</OutputNodeID>
			<OutputParameterName>input</OutputParameterName>
			<InputNodeID>4</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>float4</InputType>
			<OutputType>float4</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>2</OutputNodeID>
			<OutputParameterName>specColorSample</OutputParameterName>
			<InputNodeID>5</InputNodeID>
			<InputParameterName>rgb</InputParameterName>
			<InputType>float3</InputType>
			<OutputType>float3</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>21</OutputNodeID>
			<OutputParameterName>specColorSample</OutputParameterName>
			<InputNodeID>5</InputNodeID>
			<InputParameterName>rgb</InputParameterName>
			<InputType>float3</InputType>
			<OutputType>float3</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>20</OutputNodeID>
			<OutputParameterName>specColorSample</OutputParameterName>
			<InputNodeID>5</InputNodeID>
			<InputParameterName>rgb</InputParameterName>
			<InputType>float3</InputType>
			<OutputType>float3</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>19</OutputNodeID>
			<OutputParameterName>specColorSample</OutputParameterName>
			<InputNodeID>5</InputNodeID>
			<InputParameterName>rgb</InputParameterName>
			<InputType>float3</InputType>
			<OutputType>float3</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>2</OutputNodeID>
			<OutputParameterName>diffuseSample</OutputParameterName>
			<InputNodeID>6</InputNodeID>
			<InputParameterName>rgb</InputParameterName>
			<InputType>float3</InputType>
			<OutputType>float3</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>21</OutputNodeID>
			<OutputParameterName>diffuseSample</OutputParameterName>
			<InputNodeID>6</InputNodeID>
			<InputParameterName>rgb</InputParameterName>
			<InputType>float3</InputType>
			<OutputType>float3</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>20</OutputNodeID>
			<OutputParameterName>diffuseSample</OutputParameterName>
			<InputNodeID>6</InputNodeID>
			<InputParameterName>rgb</InputParameterName>
			<InputType>float3</InputType>
			<OutputType>float3</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>19</OutputNodeID>
			<OutputParameterName>diffuseSample</OutputParameterName>
			<InputNodeID>6</InputNodeID>
			<InputParameterName>rgb</InputParameterName>
			<InputType>float3</InputType>
			<OutputType>float3</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>8</OutputNodeID>
			<OutputParameterName>lhs</OutputParameterName>
			<InputNodeID>7</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>CommonMaterialParam</InputType>
			<OutputType>CommonMaterialParam</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>16</OutputNodeID>
			<OutputParameterName>lhs</OutputParameterName>
			<InputNodeID>8</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>CommonMaterialParam</InputType>
			<OutputType>CommonMaterialParam</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>12</OutputNodeID>
			<OutputParameterName>input</OutputParameterName>
			<InputNodeID>9</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>float4</InputType>
			<OutputType>float4</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>9</OutputNodeID>
			<OutputParameterName>lhs</OutputParameterName>
			<InputNodeID>10</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>float4</InputType>
			<OutputType>float4</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>9</OutputNodeID>
			<OutputParameterName>rhs</OutputParameterName>
			<InputNodeID>11</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>7</OutputNodeID>
			<OutputParameterName>factor</OutputParameterName>
			<InputNodeID>12</InputNodeID>
			<InputParameterName>r</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>18</OutputNodeID>
			<OutputParameterName>factor</OutputParameterName>
			<InputNodeID>12</InputNodeID>
			<InputParameterName>g</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>17</OutputNodeID>
			<OutputParameterName>factor</OutputParameterName>
			<InputNodeID>12</InputNodeID>
			<InputParameterName>b</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>15</OutputNodeID>
			<OutputParameterName>factor</OutputParameterName>
			<InputNodeID>12</InputNodeID>
			<InputParameterName>a</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>13</OutputNodeID>
			<OutputParameterName>param</OutputParameterName>
			<InputNodeID>14</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>CommonMaterialParam</InputType>
			<OutputType>CommonMaterialParam</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>14</OutputNodeID>
			<OutputParameterName>rhs</OutputParameterName>
			<InputNodeID>15</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>CommonMaterialParam</InputType>
			<OutputType>CommonMaterialParam</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>14</OutputNodeID>
			<OutputParameterName>lhs</OutputParameterName>
			<InputNodeID>16</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>CommonMaterialParam</InputType>
			<OutputType>CommonMaterialParam</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>16</OutputNodeID>
			<OutputParameterName>rhs</OutputParameterName>
			<InputNodeID>17</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>CommonMaterialParam</InputType>
			<OutputType>CommonMaterialParam</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>8</OutputNodeID>
			<OutputParameterName>rhs</OutputParameterName>
			<InputNodeID>18</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>CommonMaterialParam</InputType>
			<OutputType>CommonMaterialParam</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>15</OutputNodeID>
			<OutputParameterName>input</OutputParameterName>
			<InputNodeID>19</InputNodeID>
			<InputParameterName>materialParam</InputParameterName>
			<InputType>CommonMaterialParam</InputType>
			<OutputType>CommonMaterialParam</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>17</OutputNodeID>
			<OutputParameterName>input</OutputParameterName>
			<InputNodeID>20</InputNodeID>
			<InputParameterName>materialParam</InputParameterName>
			<InputType>CommonMaterialParam</InputType>
			<OutputType>CommonMaterialParam</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>18</OutputNodeID>
			<OutputParameterName>input</OutputParameterName>
			<InputNodeID>21</InputNodeID>
			<InputParameterName>materialParam</InputParameterName>
			<InputType>CommonMaterialParam</InputType>
			<OutputType>CommonMaterialParam</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>23</OutputNodeID>
			<OutputParameterName>input</OutputParameterName>
			<InputNodeID>22</InputNodeID>
			<InputParameterName>a</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>11</OutputNodeID>
			<OutputParameterName>third</OutputParameterName>
			<InputNodeID>22</InputNodeID>
			<InputParameterName>b</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>11</OutputNodeID>
			<OutputParameterName>second</OutputParameterName>
			<InputNodeID>22</InputNodeID>
			<InputParameterName>g</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>11</OutputNodeID>
			<OutputParameterName>first</OutputParameterName>
			<InputNodeID>22</InputNodeID>
			<InputParameterName>r</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>10</OutputNodeID>
			<OutputParameterName>r</OutputParameterName>
			<InputNodeID>22</InputNodeID>
			<InputParameterName>r</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>10</OutputNodeID>
			<OutputParameterName>g</OutputParameterName>
			<InputNodeID>22</InputNodeID>
			<InputParameterName>g</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>10</OutputNodeID>
			<OutputParameterName>b</OutputParameterName>
			<InputNodeID>22</InputNodeID>
			<InputParameterName>b</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>11</OutputNodeID>
			<OutputParameterName>forth</OutputParameterName>
			<InputNodeID>23</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>10</OutputNodeID>
			<OutputParameterName>a</OutputParameterName>
			<InputNodeID>23</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
	</NodeConnections>
	<Nodes>
		<Node>
			<FragmentArchiveName>xleres/Nodes\Texture.sh:LoadAbsolute</FragmentArchiveName>
			<NodeId>24</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>6</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/ToolsHelper\DecodeParametersTexture_ColoredSpecular.sh:DecodeParametersTexture_ColoredSpecular</FragmentArchiveName>
			<NodeId>2</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>7</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/Nodes\Texture.sh:Sample</FragmentArchiveName>
			<NodeId>3</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>8</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/Nodes\Texture.sh:Sample</FragmentArchiveName>
			<NodeId>4</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>9</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/Nodes\Basic.sh:SeparateAlpha</FragmentArchiveName>
			<NodeId>5</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>10</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/Nodes\Basic.sh:SeparateAlpha</FragmentArchiveName>
			<NodeId>6</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>11</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/Nodes\MaterialParam.sh:Scale</FragmentArchiveName>
			<NodeId>7</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>12</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/Nodes\MaterialParam.sh:Add</FragmentArchiveName>
			<NodeId>8</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>13</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/Nodes\Basic.sh:Divide4Scalar</FragmentArchiveName>
			<NodeId>9</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>14</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/Nodes\Basic.sh:Combine4</FragmentArchiveName>
			<NodeId>10</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>15</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/Nodes\Basic.sh:AddMany1</FragmentArchiveName>
			<NodeId>11</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>16</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/Nodes\Basic.sh:Separate4</FragmentArchiveName>
			<NodeId>12</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>17</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/Nodes\Output.sh:Output_ParamTex</FragmentArchiveName>
			<NodeId>13</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>18</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/Nodes\MaterialParam.sh:Add</FragmentArchiveName>
			<NodeId>14</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>19</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/Nodes\MaterialParam.sh:Scale</FragmentArchiveName>
			<NodeId>15</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>20</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/Nodes\MaterialParam.sh:Add</FragmentArchiveName>
			<NodeId>16</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>21</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/Nodes\MaterialParam.sh:Scale</FragmentArchiveName>
			<NodeId>17</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>22</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/Nodes\MaterialParam.sh:Scale</FragmentArchiveName>
			<NodeId>18</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>23</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/ToolsHelper\DecodeParametersTexture_ColoredSpecular.sh:DecodeParametersTexture_ColoredSpecular</FragmentArchiveName>
			<NodeId>19</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>24</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/ToolsHelper\DecodeParametersTexture_ColoredSpecular.sh:DecodeParametersTexture_ColoredSpecular</FragmentArchiveName>
			<NodeId>20</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>25</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/ToolsHelper\DecodeParametersTexture_ColoredSpecular.sh:DecodeParametersTexture_ColoredSpecular</FragmentArchiveName>
			<NodeId>21</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>26</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/Nodes\Basic.sh:Separate4</FragmentArchiveName>
			<NodeId>22</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>27</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/Nodes\Basic.sh:Remap1</FragmentArchiveName>
			<NodeId>23</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>28</VisualNodeId>
		</Node>
	</Nodes>
	<OutputParameterConnections>
		<OutputParameterConnection>
			<InputNodeID>13</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<Name>paramTex</Name>
			<Semantic></Semantic>
			<Type>auto</Type>
			<VisualNodeId>0</VisualNodeId>
		</OutputParameterConnection>
	</OutputParameterConnections>
	<PreviewSettingsObjects>
		<PreviewSettings>
			<Geometry>Plane2D</Geometry>
			<OutputToVisualize></OutputToVisualize>
			<VisualNodeId>6</VisualNodeId>
		</PreviewSettings>
		<PreviewSettings>
			<Geometry>Plane2D</Geometry>
			<OutputToVisualize>result2.specular</OutputToVisualize>
			<VisualNodeId>7</VisualNodeId>
		</PreviewSettings>
		<PreviewSettings>
			<Geometry>Plane2D</Geometry>
			<OutputToVisualize></OutputToVisualize>
			<VisualNodeId>8</VisualNodeId>
		</PreviewSettings>
		<PreviewSettings>
			<Geometry>Plane2D</Geometry>
			<OutputToVisualize></OutputToVisualize>
			<VisualNodeId>9</VisualNodeId>
		</PreviewSettings>
		<PreviewSettings>
			<Geometry>Plane2D</Geometry>
			<OutputToVisualize></OutputToVisualize>
			<VisualNodeId>10</VisualNodeId>
		</PreviewSettings>
		<PreviewSettings>
			<Geometry>Plane2D</Geometry>
			<OutputToVisualize></OutputToVisualize>
			<VisualNodeId>11</VisualNodeId>
		</PreviewSettings>
		<PreviewSettings>
			<Geometry>Plane2D</Geometry>
			<OutputToVisualize>result.metal</OutputToVisualize>
			<VisualNodeId>12</VisualNodeId>
		</PreviewSettings>
		<PreviewSettings>
			<Geometry>Plane2D</Geometry>
			<OutputToVisualize></OutputToVisualize>
			<VisualNodeId>13</VisualNodeId>
		</PreviewSettings>
		<PreviewSettings>
			<Geometry>Plane2D</Geometry>
			<OutputToVisualize></OutputToVisualize>
			<VisualNodeId>14</VisualNodeId>
		</PreviewSettings>
		<PreviewSettings>
			<Geometry>Plane2D</Geometry>
			<OutputToVisualize></OutputToVisualize>
			<VisualNodeId>15</VisualNodeId>
		</PreviewSettings>
		<PreviewSettings>
			<Geometry>Plane2D</Geometry>
			<OutputToVisualize></OutputToVisualize>
			<VisualNodeId>16</VisualNodeId>
		</PreviewSettings>
		<PreviewSettings>
			<Geometry>Plane2D</Geometry>
			<OutputToVisualize></OutputToVisualize>
			<VisualNodeId>17</VisualNodeId>
		</PreviewSettings>
		<PreviewSettings>
			<Geometry>Plane2D</Geometry>
			<OutputToVisualize></OutputToVisualize>
			<VisualNodeId>18</VisualNodeId>
		</PreviewSettings>
		<PreviewSettings>
			<Geometry>Plane2D</Geometry>
			<OutputToVisualize></OutputToVisualize>
			<VisualNodeId>19</VisualNodeId>
		</PreviewSettings>
		<PreviewSettings>
			<Geometry>Plane2D</Geometry>
			<OutputToVisualize></OutputToVisualize>
			<VisualNodeId>20</VisualNodeId>
		</PreviewSettings>
		<PreviewSettings>
			<Geometry>Plane2D</Geometry>
			<OutputToVisualize></OutputToVisualize>
			<VisualNodeId>21</VisualNodeId>
		</PreviewSettings>
		<PreviewSettings>
			<Geometry>Plane2D</Geometry>
			<OutputToVisualize></OutputToVisualize>
			<VisualNodeId>22</VisualNodeId>
		</PreviewSettings>
		<PreviewSettings>
			<Geometry>Plane2D</Geometry>
			<OutputToVisualize></OutputToVisualize>
			<VisualNodeId>23</VisualNodeId>
		</PreviewSettings>
		<PreviewSettings>
			<Geometry>Plane2D</Geometry>
			<OutputToVisualize></OutputToVisualize>
			<VisualNodeId>24</VisualNodeId>
		</PreviewSettings>
		<PreviewSettings>
			<Geometry>Plane2D</Geometry>
			<OutputToVisualize></OutputToVisualize>
			<VisualNodeId>25</VisualNodeId>
		</PreviewSettings>
		<PreviewSettings>
			<Geometry>Plane2D</Geometry>
			<OutputToVisualize></OutputToVisualize>
			<VisualNodeId>26</VisualNodeId>
		</PreviewSettings>
		<PreviewSettings>
			<Geometry>Plane2D</Geometry>
			<OutputToVisualize></OutputToVisualize>
			<VisualNodeId>27</VisualNodeId>
		</PreviewSettings>
		<PreviewSettings>
			<Geometry>Plane2D</Geometry>
			<OutputToVisualize></OutputToVisualize>
			<VisualNodeId>28</VisualNodeId>
		</PreviewSettings>
	</PreviewSettingsObjects>
	<VisualNodes>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>3223</d4p1:x>
				<d4p1:y>498</d4p1:y>
			</Location>
			<State>Normal</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>804</d4p1:x>
				<d4p1:y>251</d4p1:y>
			</Location>
			<State>Normal</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>804</d4p1:x>
				<d4p1:y>427</d4p1:y>
			</Location>
			<State>Normal</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>804</d4p1:x>
				<d4p1:y>83</d4p1:y>
			</Location>
			<State>Normal</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>804</d4p1:x>
				<d4p1:y>-61</d4p1:y>
			</Location>
			<State>Normal</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>-484</d4p1:x>
				<d4p1:y>288</d4p1:y>
			</Location>
			<State>Normal</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>0</d4p1:x>
				<d4p1:y>826</d4p1:y>
			</Location>
			<State>Collapsed</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>1197</d4p1:x>
				<d4p1:y>2</d4p1:y>
			</Location>
			<State>Collapsed</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>-21</d4p1:x>
				<d4p1:y>178</d4p1:y>
			</Location>
			<State>Collapsed</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>-21</d4p1:x>
				<d4p1:y>286</d4p1:y>
			</Location>
			<State>Collapsed</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>225</d4p1:x>
				<d4p1:y>286</d4p1:y>
			</Location>
			<State>Collapsed</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>225</d4p1:x>
				<d4p1:y>178</d4p1:y>
			</Location>
			<State>Collapsed</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>1824</d4p1:x>
				<d4p1:y>46</d4p1:y>
			</Location>
			<State>Collapsed</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>2028</d4p1:x>
				<d4p1:y>156</d4p1:y>
			</Location>
			<State>Collapsed</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>1061</d4p1:x>
				<d4p1:y>715</d4p1:y>
			</Location>
			<State>Collapsed</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>822</d4p1:x>
				<d4p1:y>678</d4p1:y>
			</Location>
			<State>Collapsed</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>822</d4p1:x>
				<d4p1:y>764</d4p1:y>
			</Location>
			<State>Collapsed</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>1291</d4p1:x>
				<d4p1:y>715</d4p1:y>
			</Location>
			<State>Collapsed</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>2638</d4p1:x>
				<d4p1:y>455</d4p1:y>
			</Location>
			<State>Normal</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>2383</d4p1:x>
				<d4p1:y>336</d4p1:y>
			</Location>
			<State>Collapsed</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>1824</d4p1:x>
				<d4p1:y>431</d4p1:y>
			</Location>
			<State>Collapsed</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>2205</d4p1:x>
				<d4p1:y>248</d4p1:y>
			</Location>
			<State>Collapsed</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>1824</d4p1:x>
				<d4p1:y>301</d4p1:y>
			</Location>
			<State>Collapsed</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>1824</d4p1:x>
				<d4p1:y>179</d4p1:y>
			</Location>
			<State>Collapsed</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>1193</d4p1:x>
				<d4p1:y>480</d4p1:y>
			</Location>
			<State>Collapsed</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>1193</d4p1:x>
				<d4p1:y>315</d4p1:y>
			</Location>
			<State>Collapsed</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>1193</d4p1:x>
				<d4p1:y>147</d4p1:y>
			</Location>
			<State>Collapsed</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>337</d4p1:x>
				<d4p1:y>708</d4p1:y>
			</Location>
			<State>Collapsed</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>598</d4p1:x>
				<d4p1:y>857</d4p1:y>
			</Location>
			<State>Collapsed</State>
		</VisualNode>
	</VisualNodes>
</NodeGraph>
)-- */
/* <<Chunk:NodeGraphContext:SpecularWithMask>>--(
<?xml version="1.0" encoding="utf-8"?>
<NodeGraphContext xmlns:i="http://www.w3.org/2001/XMLSchema-instance" xmlns="http://schemas.datacontract.org/2004/07/ShaderPatcherLayer">
	<HasTechniqueConfig>false</HasTechniqueConfig>
	<ShaderParameters xmlns:d2p1="http://schemas.microsoft.com/2003/10/Serialization/Arrays" />
	<Variables xmlns:d2p1="http://schemas.microsoft.com/2003/10/Serialization/Arrays" />
</NodeGraphContext>
)-- */
/* <<Chunk:CBLayout:main>>--(
float2 BRoughnessRange;
float2 BSpecularRange;
float2 BMetalRange;
float2 ARoughnessRange;
float2 ASpecularRange;
float2 AMetalRange;
float2 GRoughnessRange;
float2 GSpecularRange;
float2 GMetalRange;
float2 RRoughnessRange;
float2 RSpecularRange;
float2 RMetalRange;

)--*/

