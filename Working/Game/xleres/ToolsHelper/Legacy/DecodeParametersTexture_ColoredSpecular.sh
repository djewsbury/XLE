// CompoundDocument:1
#include "xleres/TechniqueLibrary/System/Prefix.hlsl"

#include "xleres/Nodes/Basic.sh"
#include "xleres/Nodes/MaterialParam.sh"
#include "xleres/Nodes/Color.sh"


void DecodeParametersTexture_ColoredSpecular(float2 metalRange, float2 roughnessRange, float2 specularRange, float3 diffuseSample, float3 specColorSample, out float3 finalDiffuseSample : SV_Target0, out CommonMaterialParam materialParam)
{
	float Output_12_result;
	Output_12_result = Luminance( diffuseSample );

	float Output_10_result;
	Output_10_result = Saturate1( Output_12_result );

	float Output_5_result;
	Output_5_result = Max1( Output_10_result, .02 );

	float Output_13_result;
	Output_13_result = Luminance( specColorSample );

	float Output_11_result;
	Output_11_result = Saturate1( Output_13_result );

	float Output_9_result;
	Output_9_result = Divide1( Output_11_result, Output_5_result );

	float Output_6_result;
	Output_6_result = Remap1( Output_9_result, float2(1.75f, 2.25f), float2(0.f, 1.f) );

	float Output_7_result;
	Output_7_result = Saturate1( Output_6_result );

	float Output_15_result;
	Output_15_result = Remap1( Output_7_result, float2(0,1), metalRange );

	float Output_2_result;
	Output_2_result = Power1( Output_13_result, .25 );

	float Output_14_result;
	Output_14_result = Remap1( Output_2_result, float2(0,1), float2(1,0) );

	CommonMaterialParam Output_3_result;
	Output_3_result = CommonMaterialParam_Make( Output_14_result, Output_13_result, Output_7_result );

	CommonMaterialParam Output_4_result;
	Output_4_result = ScaleByRange( Output_3_result, roughnessRange, specularRange, metalRange );

	float3 Output_8_result;
	Output_8_result = Lerp3( diffuseSample, specColorSample, Output_15_result );

	finalDiffuseSample = Output_8_result;
	materialParam = Output_4_result;

}
/* <<Chunk:NodeGraph:DecodeParametersTexture_ColoredSpecular>>--(
<?xml version="1.0" encoding="utf-8"?>
<NodeGraph xmlns:i="http://www.w3.org/2001/XMLSchema-instance" xmlns="http://schemas.datacontract.org/2004/07/ShaderPatcherLayer">
	<ConstantConnections>
		<ConstantConnection>
			<OutputNodeID>15</OutputNodeID>
			<OutputParameterName>inputRange</OutputParameterName>
			<Value>float2(0,1)</Value>
		</ConstantConnection>
		<ConstantConnection>
			<OutputNodeID>15</OutputNodeID>
			<OutputParameterName>outputRange</OutputParameterName>
			<Value>&lt;metalRange&gt;</Value>
		</ConstantConnection>
		<ConstantConnection>
			<OutputNodeID>2</OutputNodeID>
			<OutputParameterName>exponent</OutputParameterName>
			<Value>.25</Value>
		</ConstantConnection>
		<ConstantConnection>
			<OutputNodeID>4</OutputNodeID>
			<OutputParameterName>rRange</OutputParameterName>
			<Value>&lt;roughnessRange&gt;</Value>
		</ConstantConnection>
		<ConstantConnection>
			<OutputNodeID>4</OutputNodeID>
			<OutputParameterName>sRange</OutputParameterName>
			<Value>&lt;specularRange&gt;</Value>
		</ConstantConnection>
		<ConstantConnection>
			<OutputNodeID>4</OutputNodeID>
			<OutputParameterName>mRange</OutputParameterName>
			<Value>&lt;metalRange&gt;</Value>
		</ConstantConnection>
		<ConstantConnection>
			<OutputNodeID>5</OutputNodeID>
			<OutputParameterName>rhs</OutputParameterName>
			<Value>.02</Value>
		</ConstantConnection>
		<ConstantConnection>
			<OutputNodeID>6</OutputNodeID>
			<OutputParameterName>inputRange</OutputParameterName>
			<Value>float2(1.75f, 2.25f)</Value>
		</ConstantConnection>
		<ConstantConnection>
			<OutputNodeID>6</OutputNodeID>
			<OutputParameterName>outputRange</OutputParameterName>
			<Value>float2(0.f, 1.f)</Value>
		</ConstantConnection>
		<ConstantConnection>
			<OutputNodeID>14</OutputNodeID>
			<OutputParameterName>inputRange</OutputParameterName>
			<Value>float2(0,1)</Value>
		</ConstantConnection>
		<ConstantConnection>
			<OutputNodeID>14</OutputNodeID>
			<OutputParameterName>outputRange</OutputParameterName>
			<Value>float2(1,0)</Value>
		</ConstantConnection>
	</ConstantConnections>
	<InputParameterConnections>
		<InputParameterConnection>
			<OutputNodeID>12</OutputNodeID>
			<OutputParameterName>srgbInput</OutputParameterName>
			<Default i:nil="true" />
			<Name>&lt;diffuseSample&gt;</Name>
			<Semantic></Semantic>
			<Type>auto</Type>
			<VisualNodeId>1</VisualNodeId>
		</InputParameterConnection>
		<InputParameterConnection>
			<OutputNodeID>13</OutputNodeID>
			<OutputParameterName>srgbInput</OutputParameterName>
			<Default i:nil="true" />
			<Name>&lt;specColorSample&gt;</Name>
			<Semantic></Semantic>
			<Type>auto</Type>
			<VisualNodeId>1</VisualNodeId>
		</InputParameterConnection>
		<InputParameterConnection>
			<OutputNodeID>8</OutputNodeID>
			<OutputParameterName>min</OutputParameterName>
			<Default i:nil="true" />
			<Name>&lt;diffuseSample&gt;</Name>
			<Semantic></Semantic>
			<Type>auto</Type>
			<VisualNodeId>1</VisualNodeId>
		</InputParameterConnection>
		<InputParameterConnection>
			<OutputNodeID>8</OutputNodeID>
			<OutputParameterName>max</OutputParameterName>
			<Default i:nil="true" />
			<Name>&lt;specColorSample&gt;</Name>
			<Semantic></Semantic>
			<Type>auto</Type>
			<VisualNodeId>1</VisualNodeId>
		</InputParameterConnection>
	</InputParameterConnections>
	<NodeConnections>
		<NodeConnection>
			<OutputNodeID>8</OutputNodeID>
			<OutputParameterName>alpha</OutputParameterName>
			<InputNodeID>15</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>14</OutputNodeID>
			<OutputParameterName>input</OutputParameterName>
			<InputNodeID>2</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>4</OutputNodeID>
			<OutputParameterName>input</OutputParameterName>
			<InputNodeID>3</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>CommonMaterialParam</InputType>
			<OutputType>CommonMaterialParam</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>9</OutputNodeID>
			<OutputParameterName>rhs</OutputParameterName>
			<InputNodeID>5</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>7</OutputNodeID>
			<OutputParameterName>input</OutputParameterName>
			<InputNodeID>6</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>3</OutputNodeID>
			<OutputParameterName>metal</OutputParameterName>
			<InputNodeID>7</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>15</OutputNodeID>
			<OutputParameterName>input</OutputParameterName>
			<InputNodeID>7</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>6</OutputNodeID>
			<OutputParameterName>input</OutputParameterName>
			<InputNodeID>9</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>5</OutputNodeID>
			<OutputParameterName>lhs</OutputParameterName>
			<InputNodeID>10</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>9</OutputNodeID>
			<OutputParameterName>lhs</OutputParameterName>
			<InputNodeID>11</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>10</OutputNodeID>
			<OutputParameterName>input</OutputParameterName>
			<InputNodeID>12</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>11</OutputNodeID>
			<OutputParameterName>input</OutputParameterName>
			<InputNodeID>13</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>3</OutputNodeID>
			<OutputParameterName>specular</OutputParameterName>
			<InputNodeID>13</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>2</OutputNodeID>
			<OutputParameterName>base</OutputParameterName>
			<InputNodeID>13</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>3</OutputNodeID>
			<OutputParameterName>roughness</OutputParameterName>
			<InputNodeID>14</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
	</NodeConnections>
	<Nodes>
		<Node>
			<FragmentArchiveName>xleres/Nodes\Basic.sh:Remap1</FragmentArchiveName>
			<NodeId>15</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>2</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/Nodes\Basic.sh:Power1</FragmentArchiveName>
			<NodeId>2</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>3</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/Nodes\MaterialParam.sh:CommonMaterialParam_Make</FragmentArchiveName>
			<NodeId>3</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>4</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/Nodes\MaterialParam.sh:ScaleByRange</FragmentArchiveName>
			<NodeId>4</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>5</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/Nodes\Basic.sh:Max1</FragmentArchiveName>
			<NodeId>5</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>6</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/Nodes\Basic.sh:Remap1</FragmentArchiveName>
			<NodeId>6</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>7</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/Nodes\Basic.sh:Saturate1</FragmentArchiveName>
			<NodeId>7</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>8</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/Nodes\Basic.sh:Lerp3</FragmentArchiveName>
			<NodeId>8</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>9</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/Nodes\Basic.sh:Divide1</FragmentArchiveName>
			<NodeId>9</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>10</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/Nodes\Basic.sh:Saturate1</FragmentArchiveName>
			<NodeId>10</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>11</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/Nodes\Basic.sh:Saturate1</FragmentArchiveName>
			<NodeId>11</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>12</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/Nodes\Color.sh:Luminance</FragmentArchiveName>
			<NodeId>12</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>13</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/Nodes\Color.sh:Luminance</FragmentArchiveName>
			<NodeId>13</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>14</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/Nodes\Basic.sh:Remap1</FragmentArchiveName>
			<NodeId>14</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>15</VisualNodeId>
		</Node>
	</Nodes>
	<OutputParameterConnections>
		<OutputParameterConnection>
			<InputNodeID>8</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<Name>finalDiffuseSample</Name>
			<Semantic></Semantic>
			<Type>auto</Type>
			<VisualNodeId>0</VisualNodeId>
		</OutputParameterConnection>
		<OutputParameterConnection>
			<InputNodeID>4</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<Name>materialParam</Name>
			<Semantic></Semantic>
			<Type>auto</Type>
			<VisualNodeId>0</VisualNodeId>
		</OutputParameterConnection>
	</OutputParameterConnections>
	<PreviewSettingsObjects>
		<PreviewSettings>
			<Geometry>Plane2D</Geometry>
			<OutputToVisualize></OutputToVisualize>
			<VisualNodeId>2</VisualNodeId>
		</PreviewSettings>
		<PreviewSettings>
			<Geometry>Chart</Geometry>
			<OutputToVisualize></OutputToVisualize>
			<VisualNodeId>3</VisualNodeId>
		</PreviewSettings>
		<PreviewSettings>
			<Geometry>Plane2D</Geometry>
			<OutputToVisualize>result.metal</OutputToVisualize>
			<VisualNodeId>4</VisualNodeId>
		</PreviewSettings>
		<PreviewSettings>
			<Geometry>Chart</Geometry>
			<OutputToVisualize>result.metal</OutputToVisualize>
			<VisualNodeId>5</VisualNodeId>
		</PreviewSettings>
		<PreviewSettings>
			<Geometry>Chart</Geometry>
			<OutputToVisualize></OutputToVisualize>
			<VisualNodeId>6</VisualNodeId>
		</PreviewSettings>
		<PreviewSettings>
			<Geometry>Chart</Geometry>
			<OutputToVisualize></OutputToVisualize>
			<VisualNodeId>7</VisualNodeId>
		</PreviewSettings>
		<PreviewSettings>
			<Geometry>Chart</Geometry>
			<OutputToVisualize></OutputToVisualize>
			<VisualNodeId>8</VisualNodeId>
		</PreviewSettings>
		<PreviewSettings>
			<Geometry>Chart</Geometry>
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
			<OutputToVisualize></OutputToVisualize>
			<VisualNodeId>12</VisualNodeId>
		</PreviewSettings>
		<PreviewSettings>
			<Geometry>Plane2D</Geometry>
			<OutputToVisualize></OutputToVisualize>
			<VisualNodeId>13</VisualNodeId>
		</PreviewSettings>
		<PreviewSettings>
			<Geometry>Chart</Geometry>
			<OutputToVisualize></OutputToVisualize>
			<VisualNodeId>14</VisualNodeId>
		</PreviewSettings>
		<PreviewSettings>
			<Geometry>Chart</Geometry>
			<OutputToVisualize></OutputToVisualize>
			<VisualNodeId>15</VisualNodeId>
		</PreviewSettings>
	</PreviewSettingsObjects>
	<VisualNodes>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>2513</d4p1:x>
				<d4p1:y>460</d4p1:y>
			</Location>
			<State>Normal</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>-450</d4p1:x>
				<d4p1:y>273</d4p1:y>
			</Location>
			<State>Normal</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>1659</d4p1:x>
				<d4p1:y>592</d4p1:y>
			</Location>
			<State>Collapsed</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>681</d4p1:x>
				<d4p1:y>-15</d4p1:y>
			</Location>
			<State>Normal</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>1517</d4p1:x>
				<d4p1:y>351</d4p1:y>
			</Location>
			<State>Collapsed</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>1860</d4p1:x>
				<d4p1:y>229</d4p1:y>
			</Location>
			<State>Collapsed</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>600</d4p1:x>
				<d4p1:y>597</d4p1:y>
			</Location>
			<State>Collapsed</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>1038</d4p1:x>
				<d4p1:y>656</d4p1:y>
			</Location>
			<State>Normal</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>1297</d4p1:x>
				<d4p1:y>557</d4p1:y>
			</Location>
			<State>Collapsed</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>2011</d4p1:x>
				<d4p1:y>501</d4p1:y>
			</Location>
			<State>Collapsed</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>800</d4p1:x>
				<d4p1:y>537</d4p1:y>
			</Location>
			<State>Collapsed</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>406</d4p1:x>
				<d4p1:y>572</d4p1:y>
			</Location>
			<State>Collapsed</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>406</d4p1:x>
				<d4p1:y>500</d4p1:y>
			</Location>
			<State>Collapsed</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>164</d4p1:x>
				<d4p1:y>572</d4p1:y>
			</Location>
			<State>Collapsed</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>164</d4p1:x>
				<d4p1:y>456</d4p1:y>
			</Location>
			<State>Collapsed</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>1197</d4p1:x>
				<d4p1:y>-15</d4p1:y>
			</Location>
			<State>Normal</State>
		</VisualNode>
	</VisualNodes>
</NodeGraph>
)-- */
/* <<Chunk:NodeGraphContext:DecodeParametersTexture_ColoredSpecular>>--(
<?xml version="1.0" encoding="utf-8"?>
<NodeGraphContext xmlns:i="http://www.w3.org/2001/XMLSchema-instance" xmlns="http://schemas.datacontract.org/2004/07/ShaderPatcherLayer">
	<HasTechniqueConfig>false</HasTechniqueConfig>
	<ShaderParameters xmlns:d2p1="http://schemas.microsoft.com/2003/10/Serialization/Arrays" />
	<Variables xmlns:d2p1="http://schemas.microsoft.com/2003/10/Serialization/Arrays">
		<d2p1:KeyValueOfstringstring>
			<d2p1:Key>diffuseSample</d2p1:Key>
			<d2p1:Value>0.5.xxx</d2p1:Value>
		</d2p1:KeyValueOfstringstring>
		<d2p1:KeyValueOfstringstring>
			<d2p1:Key>specColorSample</d2p1:Key>
			<d2p1:Value>AutoAxis:0.0.xxx:1.0.xxx</d2p1:Value>
		</d2p1:KeyValueOfstringstring>
		<d2p1:KeyValueOfstringstring>
			<d2p1:Key>roughnessRange</d2p1:Key>
			<d2p1:Value>float2(0, 1)</d2p1:Value>
		</d2p1:KeyValueOfstringstring>
		<d2p1:KeyValueOfstringstring>
			<d2p1:Key>specularRange</d2p1:Key>
			<d2p1:Value>float2(0, 1)</d2p1:Value>
		</d2p1:KeyValueOfstringstring>
		<d2p1:KeyValueOfstringstring>
			<d2p1:Key>metalRange</d2p1:Key>
			<d2p1:Value>float2(0, 1)</d2p1:Value>
		</d2p1:KeyValueOfstringstring>
	</Variables>
</NodeGraphContext>
)-- */
