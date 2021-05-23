// CompoundDocument:1
#include "xleres/TechniqueLibrary/System/Prefix.hlsl"

#include "xleres/Nodes/Basic.sh"
#include "xleres/TechniqueLibrary/LightingEngine/SpecularMethods.hlsl"
#include "xleres/TechniqueLibrary/LightingEngine/LightingAlgorithm.hlsl"


void GGXTransmission(float roughness, float iorIncident, float iorOutgoing, float3 i, float3 o, float3 n, out float result : SV_Target0)
{
	float3 Output_21_result;
	Output_21_result = CalculateHt( i, o, iorIncident, iorOutgoing );

	float Output_19_result;
	Output_19_result = Dot3( Output_21_result, n );

	float Output_23_result;
	Output_23_result = RoughnessToDAlpha( roughness );

	float Output_16_result;
	Output_16_result = TrowReitzD( Output_19_result, Output_23_result );

	float Output_30_result;
	Output_30_result = Dot3( i, n );

	float Output_28_result;
	Output_28_result = Abs1( Output_30_result );

	float Output_33_result;
	Output_33_result = RoughnessToGAlpha( roughness );

	float Output_26_result;
	Output_26_result = SmithG( Output_28_result, Output_33_result );

	float Output_29_result;
	Output_29_result = Dot3( o, n );

	float Output_35_result;
	Output_35_result = Abs1( Output_29_result );

	float Output_36_result;
	Output_36_result = SmithG( Output_35_result, Output_33_result );

	float Output_17_result;
	Output_17_result = Multiply1( Output_26_result, Output_36_result );

	float Output_27_result;
	Output_27_result = Multiply1( Output_28_result, Output_35_result );

	float Output_24_result;
	Output_24_result = Dot3( Output_21_result, i );

	float Output_25_result;
	Output_25_result = Abs1( Output_24_result );

	float Output_40_result;
	Output_40_result = Dot3( Output_21_result, o );

	float Output_34_result;
	Output_34_result = Abs1( Output_40_result );

	float Output_39_result;
	Output_39_result = Multiply1( Output_25_result, Output_34_result );

	float Output_37_result;
	Output_37_result = Divide1( Output_39_result, Output_27_result );

	float Output_18_result;
	Output_18_result = Square1( iorOutgoing );

	float Output_20_result;
	Output_20_result = Multiply1( iorOutgoing, Output_40_result );

	float Output_22_result;
	Output_22_result = Multiply1( iorIncident, Output_24_result );

	float Output_66_result;
	Output_66_result = Subtract1( Output_22_result, Output_20_result );

	float Output_31_result;
	Output_31_result = Square1( Output_66_result );

	float Output_38_result;
	Output_38_result = Divide1( Output_18_result, Output_31_result );

	float Output_15_result;
	Output_15_result = MultiplyMany1( Output_37_result, Output_17_result, Output_16_result, Output_38_result );

	result = Output_15_result;

}
/* <<Chunk:NodeGraph:GGXTransmission>>--(
<?xml version="1.0" encoding="utf-8"?>
<NodeGraph xmlns:i="http://www.w3.org/2001/XMLSchema-instance" xmlns="http://schemas.datacontract.org/2004/07/ShaderPatcherLayer">
	<ConstantConnections>
		<ConstantConnection>
			<OutputNodeID>23</OutputNodeID>
			<OutputParameterName>roughness</OutputParameterName>
			<Value>&lt;roughness&gt;</Value>
		</ConstantConnection>
		<ConstantConnection>
			<OutputNodeID>33</OutputNodeID>
			<OutputParameterName>roughness</OutputParameterName>
			<Value>&lt;roughness&gt;</Value>
		</ConstantConnection>
		<ConstantConnection>
			<OutputNodeID>21</OutputNodeID>
			<OutputParameterName>iorIncident</OutputParameterName>
			<Value>&lt;iorIncident&gt;</Value>
		</ConstantConnection>
		<ConstantConnection>
			<OutputNodeID>21</OutputNodeID>
			<OutputParameterName>iorOutgoing</OutputParameterName>
			<Value>&lt;iorOutgoing&gt;</Value>
		</ConstantConnection>
	</ConstantConnections>
	<InputParameterConnections>
		<InputParameterConnection>
			<OutputNodeID>20</OutputNodeID>
			<OutputParameterName>lhs</OutputParameterName>
			<Default></Default>
			<Name>&lt;iorOutgoing&gt;</Name>
			<Semantic></Semantic>
			<Type>auto</Type>
			<VisualNodeId>17</VisualNodeId>
		</InputParameterConnection>
		<InputParameterConnection>
			<OutputNodeID>22</OutputNodeID>
			<OutputParameterName>lhs</OutputParameterName>
			<Default></Default>
			<Name>&lt;iorIncident&gt;</Name>
			<Semantic></Semantic>
			<Type>auto</Type>
			<VisualNodeId>17</VisualNodeId>
		</InputParameterConnection>
		<InputParameterConnection>
			<OutputNodeID>18</OutputNodeID>
			<OutputParameterName>value</OutputParameterName>
			<Default></Default>
			<Name>&lt;iorOutgoing&gt;</Name>
			<Semantic></Semantic>
			<Type>auto</Type>
			<VisualNodeId>17</VisualNodeId>
		</InputParameterConnection>
		<InputParameterConnection>
			<OutputNodeID>21</OutputNodeID>
			<OutputParameterName>i</OutputParameterName>
			<Default i:nil="true" />
			<Name>&lt;i&gt;</Name>
			<Semantic></Semantic>
			<Type>auto</Type>
			<VisualNodeId>26</VisualNodeId>
		</InputParameterConnection>
		<InputParameterConnection>
			<OutputNodeID>21</OutputNodeID>
			<OutputParameterName>o</OutputParameterName>
			<Default></Default>
			<Name>&lt;o&gt;</Name>
			<Semantic></Semantic>
			<Type>auto</Type>
			<VisualNodeId>26</VisualNodeId>
		</InputParameterConnection>
		<InputParameterConnection>
			<OutputNodeID>24</OutputNodeID>
			<OutputParameterName>rhs</OutputParameterName>
			<Default i:nil="true" />
			<Name>&lt;i&gt;</Name>
			<Semantic></Semantic>
			<Type>auto</Type>
			<VisualNodeId>26</VisualNodeId>
		</InputParameterConnection>
		<InputParameterConnection>
			<OutputNodeID>40</OutputNodeID>
			<OutputParameterName>rhs</OutputParameterName>
			<Default></Default>
			<Name>&lt;o&gt;</Name>
			<Semantic></Semantic>
			<Type>auto</Type>
			<VisualNodeId>26</VisualNodeId>
		</InputParameterConnection>
		<InputParameterConnection>
			<OutputNodeID>30</OutputNodeID>
			<OutputParameterName>lhs</OutputParameterName>
			<Default i:nil="true" />
			<Name>&lt;i&gt;</Name>
			<Semantic></Semantic>
			<Type>auto</Type>
			<VisualNodeId>26</VisualNodeId>
		</InputParameterConnection>
		<InputParameterConnection>
			<OutputNodeID>30</OutputNodeID>
			<OutputParameterName>rhs</OutputParameterName>
			<Default></Default>
			<Name>&lt;n&gt;</Name>
			<Semantic></Semantic>
			<Type>auto</Type>
			<VisualNodeId>26</VisualNodeId>
		</InputParameterConnection>
		<InputParameterConnection>
			<OutputNodeID>29</OutputNodeID>
			<OutputParameterName>lhs</OutputParameterName>
			<Default></Default>
			<Name>&lt;o&gt;</Name>
			<Semantic></Semantic>
			<Type>auto</Type>
			<VisualNodeId>26</VisualNodeId>
		</InputParameterConnection>
		<InputParameterConnection>
			<OutputNodeID>29</OutputNodeID>
			<OutputParameterName>rhs</OutputParameterName>
			<Default></Default>
			<Name>&lt;n&gt;</Name>
			<Semantic></Semantic>
			<Type>auto</Type>
			<VisualNodeId>26</VisualNodeId>
		</InputParameterConnection>
		<InputParameterConnection>
			<OutputNodeID>19</OutputNodeID>
			<OutputParameterName>rhs</OutputParameterName>
			<Default></Default>
			<Name>&lt;n&gt;</Name>
			<Semantic></Semantic>
			<Type>auto</Type>
			<VisualNodeId>26</VisualNodeId>
		</InputParameterConnection>
	</InputParameterConnections>
	<NodeConnections>
		<NodeConnection>
			<OutputNodeID>31</OutputNodeID>
			<OutputParameterName>value</OutputParameterName>
			<InputNodeID>66</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>66</OutputNodeID>
			<OutputParameterName>rhs</OutputParameterName>
			<InputNodeID>20</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>66</OutputNodeID>
			<OutputParameterName>lhs</OutputParameterName>
			<InputNodeID>22</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>16</OutputNodeID>
			<OutputParameterName>alpha</OutputParameterName>
			<InputNodeID>23</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>37</OutputNodeID>
			<OutputParameterName>lhs</OutputParameterName>
			<InputNodeID>39</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>37</OutputNodeID>
			<OutputParameterName>rhs</OutputParameterName>
			<InputNodeID>27</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>16</OutputNodeID>
			<OutputParameterName>NdotH</OutputParameterName>
			<InputNodeID>19</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>35</OutputNodeID>
			<OutputParameterName>value</OutputParameterName>
			<InputNodeID>29</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>27</OutputNodeID>
			<OutputParameterName>rhs</OutputParameterName>
			<InputNodeID>35</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>36</OutputNodeID>
			<OutputParameterName>NdotV</OutputParameterName>
			<InputNodeID>35</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>34</OutputNodeID>
			<OutputParameterName>value</OutputParameterName>
			<InputNodeID>40</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>20</OutputNodeID>
			<OutputParameterName>rhs</OutputParameterName>
			<InputNodeID>40</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>39</OutputNodeID>
			<OutputParameterName>rhs</OutputParameterName>
			<InputNodeID>34</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>26</OutputNodeID>
			<OutputParameterName>NdotV</OutputParameterName>
			<InputNodeID>28</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>27</OutputNodeID>
			<OutputParameterName>lhs</OutputParameterName>
			<InputNodeID>28</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>39</OutputNodeID>
			<OutputParameterName>lhs</OutputParameterName>
			<InputNodeID>25</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>25</OutputNodeID>
			<OutputParameterName>value</OutputParameterName>
			<InputNodeID>24</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>22</OutputNodeID>
			<OutputParameterName>rhs</OutputParameterName>
			<InputNodeID>24</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>28</OutputNodeID>
			<OutputParameterName>value</OutputParameterName>
			<InputNodeID>30</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>15</OutputNodeID>
			<OutputParameterName>forth</OutputParameterName>
			<InputNodeID>38</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>38</OutputNodeID>
			<OutputParameterName>rhs</OutputParameterName>
			<InputNodeID>31</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>15</OutputNodeID>
			<OutputParameterName>first</OutputParameterName>
			<InputNodeID>37</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>36</OutputNodeID>
			<OutputParameterName>alpha</OutputParameterName>
			<InputNodeID>33</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>26</OutputNodeID>
			<OutputParameterName>alpha</OutputParameterName>
			<InputNodeID>33</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>17</OutputNodeID>
			<OutputParameterName>lhs</OutputParameterName>
			<InputNodeID>26</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>17</OutputNodeID>
			<OutputParameterName>rhs</OutputParameterName>
			<InputNodeID>36</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>15</OutputNodeID>
			<OutputParameterName>second</OutputParameterName>
			<InputNodeID>17</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>15</OutputNodeID>
			<OutputParameterName>third</OutputParameterName>
			<InputNodeID>16</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>38</OutputNodeID>
			<OutputParameterName>lhs</OutputParameterName>
			<InputNodeID>18</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>float</InputType>
			<OutputType>float</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>24</OutputNodeID>
			<OutputParameterName>lhs</OutputParameterName>
			<InputNodeID>21</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>float3</InputType>
			<OutputType>float3</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>40</OutputNodeID>
			<OutputParameterName>lhs</OutputParameterName>
			<InputNodeID>21</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>float3</InputType>
			<OutputType>float3</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
		<NodeConnection>
			<OutputNodeID>19</OutputNodeID>
			<OutputParameterName>lhs</OutputParameterName>
			<InputNodeID>21</InputNodeID>
			<InputParameterName>result</InputParameterName>
			<InputType>float3</InputType>
			<OutputType>float3</OutputType>
			<Semantic i:nil="true" />
		</NodeConnection>
	</NodeConnections>
	<Nodes>
		<Node>
			<FragmentArchiveName>xleres/Nodes\Basic.sh:Subtract1</FragmentArchiveName>
			<NodeId>66</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>0</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/Nodes\Basic.sh:Multiply1</FragmentArchiveName>
			<NodeId>20</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>1</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/Nodes\Basic.sh:Multiply1</FragmentArchiveName>
			<NodeId>22</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>2</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/Lighting\SpecularMethods.h:RoughnessToDAlpha</FragmentArchiveName>
			<NodeId>23</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>3</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/Nodes\Basic.sh:Multiply1</FragmentArchiveName>
			<NodeId>39</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>4</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/Nodes\Basic.sh:Multiply1</FragmentArchiveName>
			<NodeId>27</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>5</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/Nodes\Basic.sh:Dot3</FragmentArchiveName>
			<NodeId>19</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>6</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/Nodes\Basic.sh:Dot3</FragmentArchiveName>
			<NodeId>29</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>7</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/Nodes\Basic.sh:Abs1</FragmentArchiveName>
			<NodeId>35</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>8</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/Nodes\Basic.sh:Dot3</FragmentArchiveName>
			<NodeId>40</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>9</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/Nodes\Basic.sh:Abs1</FragmentArchiveName>
			<NodeId>34</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>10</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/Nodes\Basic.sh:Abs1</FragmentArchiveName>
			<NodeId>28</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>11</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/Nodes\Basic.sh:Abs1</FragmentArchiveName>
			<NodeId>25</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>12</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/Nodes\Basic.sh:Dot3</FragmentArchiveName>
			<NodeId>24</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>13</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/Nodes\Basic.sh:Dot3</FragmentArchiveName>
			<NodeId>30</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>14</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/Nodes\Basic.sh:Divide1</FragmentArchiveName>
			<NodeId>38</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>15</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/Nodes\Basic.sh:Square1</FragmentArchiveName>
			<NodeId>31</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>16</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/Nodes\Basic.sh:MultiplyMany1</FragmentArchiveName>
			<NodeId>15</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>18</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/Nodes\Basic.sh:Divide1</FragmentArchiveName>
			<NodeId>37</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>19</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/Lighting\SpecularMethods.h:RoughnessToGAlpha</FragmentArchiveName>
			<NodeId>33</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>20</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/Lighting\SpecularMethods.h:SmithG</FragmentArchiveName>
			<NodeId>26</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>21</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/Lighting\SpecularMethods.h:SmithG</FragmentArchiveName>
			<NodeId>36</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>22</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/Nodes\Basic.sh:Multiply1</FragmentArchiveName>
			<NodeId>17</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>23</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/Lighting\SpecularMethods.h:TrowReitzD</FragmentArchiveName>
			<NodeId>16</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>24</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/Nodes\Basic.sh:Square1</FragmentArchiveName>
			<NodeId>18</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>25</VisualNodeId>
		</Node>
		<Node>
			<FragmentArchiveName>xleres/Lighting\LightingAlgorithm.h:CalculateHt</FragmentArchiveName>
			<NodeId>21</NodeId>
			<NodeType>Procedure</NodeType>
			<VisualNodeId>27</VisualNodeId>
		</Node>
	</Nodes>
	<OutputParameterConnections />
	<PreviewSettingsObjects>
		<PreviewSettings>
			<Geometry>Plane2D</Geometry>
			<OutputToVisualize></OutputToVisualize>
			<VisualNodeId>0</VisualNodeId>
		</PreviewSettings>
		<PreviewSettings>
			<Geometry>Plane2D</Geometry>
			<OutputToVisualize></OutputToVisualize>
			<VisualNodeId>1</VisualNodeId>
		</PreviewSettings>
		<PreviewSettings>
			<Geometry>Plane2D</Geometry>
			<OutputToVisualize></OutputToVisualize>
			<VisualNodeId>2</VisualNodeId>
		</PreviewSettings>
		<PreviewSettings>
			<Geometry>Plane2D</Geometry>
			<OutputToVisualize></OutputToVisualize>
			<VisualNodeId>3</VisualNodeId>
		</PreviewSettings>
		<PreviewSettings>
			<Geometry>Sphere</Geometry>
			<OutputToVisualize></OutputToVisualize>
			<VisualNodeId>4</VisualNodeId>
		</PreviewSettings>
		<PreviewSettings>
			<Geometry>Sphere</Geometry>
			<OutputToVisualize></OutputToVisualize>
			<VisualNodeId>5</VisualNodeId>
		</PreviewSettings>
		<PreviewSettings>
			<Geometry>Sphere</Geometry>
			<OutputToVisualize></OutputToVisualize>
			<VisualNodeId>6</VisualNodeId>
		</PreviewSettings>
		<PreviewSettings>
			<Geometry>Sphere</Geometry>
			<OutputToVisualize></OutputToVisualize>
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
			<Geometry>Sphere</Geometry>
			<OutputToVisualize></OutputToVisualize>
			<VisualNodeId>12</VisualNodeId>
		</PreviewSettings>
		<PreviewSettings>
			<Geometry>Sphere</Geometry>
			<OutputToVisualize></OutputToVisualize>
			<VisualNodeId>13</VisualNodeId>
		</PreviewSettings>
		<PreviewSettings>
			<Geometry>Sphere</Geometry>
			<OutputToVisualize></OutputToVisualize>
			<VisualNodeId>14</VisualNodeId>
		</PreviewSettings>
		<PreviewSettings>
			<Geometry>Sphere</Geometry>
			<OutputToVisualize>result / 100</OutputToVisualize>
			<VisualNodeId>15</VisualNodeId>
		</PreviewSettings>
		<PreviewSettings>
			<Geometry>Plane2D</Geometry>
			<OutputToVisualize></OutputToVisualize>
			<VisualNodeId>16</VisualNodeId>
		</PreviewSettings>
		<PreviewSettings>
			<Geometry>Sphere</Geometry>
			<OutputToVisualize></OutputToVisualize>
			<VisualNodeId>18</VisualNodeId>
		</PreviewSettings>
		<PreviewSettings>
			<Geometry>Sphere</Geometry>
			<OutputToVisualize></OutputToVisualize>
			<VisualNodeId>19</VisualNodeId>
		</PreviewSettings>
		<PreviewSettings>
			<Geometry>Plane2D</Geometry>
			<OutputToVisualize></OutputToVisualize>
			<VisualNodeId>20</VisualNodeId>
		</PreviewSettings>
		<PreviewSettings>
			<Geometry>Sphere</Geometry>
			<OutputToVisualize></OutputToVisualize>
			<VisualNodeId>21</VisualNodeId>
		</PreviewSettings>
		<PreviewSettings>
			<Geometry>Plane2D</Geometry>
			<OutputToVisualize></OutputToVisualize>
			<VisualNodeId>22</VisualNodeId>
		</PreviewSettings>
		<PreviewSettings>
			<Geometry>Sphere</Geometry>
			<OutputToVisualize></OutputToVisualize>
			<VisualNodeId>23</VisualNodeId>
		</PreviewSettings>
		<PreviewSettings>
			<Geometry>Sphere</Geometry>
			<OutputToVisualize></OutputToVisualize>
			<VisualNodeId>24</VisualNodeId>
		</PreviewSettings>
		<PreviewSettings>
			<Geometry>Plane2D</Geometry>
			<OutputToVisualize></OutputToVisualize>
			<VisualNodeId>25</VisualNodeId>
		</PreviewSettings>
		<PreviewSettings>
			<Geometry>Sphere</Geometry>
			<OutputToVisualize>result * .5 + .5.xxx</OutputToVisualize>
			<VisualNodeId>27</VisualNodeId>
		</PreviewSettings>
	</PreviewSettingsObjects>
	<VisualNodes>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>664</d4p1:x>
				<d4p1:y>1216</d4p1:y>
			</Location>
			<State>Collapsed</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>475</d4p1:x>
				<d4p1:y>1244</d4p1:y>
			</Location>
			<State>Collapsed</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>475</d4p1:x>
				<d4p1:y>1163</d4p1:y>
			</Location>
			<State>Collapsed</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>820</d4p1:x>
				<d4p1:y>947</d4p1:y>
			</Location>
			<State>Collapsed</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>658</d4p1:x>
				<d4p1:y>327</d4p1:y>
			</Location>
			<State>Collapsed</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>657</d4p1:x>
				<d4p1:y>424</d4p1:y>
			</Location>
			<State>Collapsed</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>-106</d4p1:x>
				<d4p1:y>872</d4p1:y>
			</Location>
			<State>Collapsed</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>-106</d4p1:x>
				<d4p1:y>637</d4p1:y>
			</Location>
			<State>Collapsed</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>218</d4p1:x>
				<d4p1:y>638</d4p1:y>
			</Location>
			<State>Collapsed</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>-106</d4p1:x>
				<d4p1:y>504</d4p1:y>
			</Location>
			<State>Collapsed</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>218</d4p1:x>
				<d4p1:y>505</d4p1:y>
			</Location>
			<State>Collapsed</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>218</d4p1:x>
				<d4p1:y>572</d4p1:y>
			</Location>
			<State>Collapsed</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>218</d4p1:x>
				<d4p1:y>442</d4p1:y>
			</Location>
			<State>Collapsed</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>-106</d4p1:x>
				<d4p1:y>438</d4p1:y>
			</Location>
			<State>Collapsed</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>-106</d4p1:x>
				<d4p1:y>572</d4p1:y>
			</Location>
			<State>Collapsed</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>1164</d4p1:x>
				<d4p1:y>1169</d4p1:y>
			</Location>
			<State>Normal</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>830</d4p1:x>
				<d4p1:y>1216</d4p1:y>
			</Location>
			<State>Collapsed</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>-48</d4p1:x>
				<d4p1:y>1093</d4p1:y>
			</Location>
			<State>Normal</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>1830</d4p1:x>
				<d4p1:y>715</d4p1:y>
			</Location>
			<State>Normal</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>1164</d4p1:x>
				<d4p1:y>258</d4p1:y>
			</Location>
			<State>Normal</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>666</d4p1:x>
				<d4p1:y>691</d4p1:y>
			</Location>
			<State>Collapsed</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>932</d4p1:x>
				<d4p1:y>561</d4p1:y>
			</Location>
			<State>Collapsed</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>932</d4p1:x>
				<d4p1:y>612</d4p1:y>
			</Location>
			<State>Collapsed</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>1164</d4p1:x>
				<d4p1:y>567</d4p1:y>
			</Location>
			<State>Normal</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>1164</d4p1:x>
				<d4p1:y>869</d4p1:y>
			</Location>
			<State>Normal</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>835</d4p1:x>
				<d4p1:y>1109</d4p1:y>
			</Location>
			<State>Collapsed</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>-836.2657</d4p1:x>
				<d4p1:y>582.577942</d4p1:y>
			</Location>
			<State>Normal</State>
		</VisualNode>
		<VisualNode>
			<Location xmlns:d4p1="http://schemas.datacontract.org/2004/07/System.Drawing">
				<d4p1:x>-561</d4p1:x>
				<d4p1:y>429</d4p1:y>
			</Location>
			<State>Collapsed</State>
		</VisualNode>
	</VisualNodes>
</NodeGraph>
)-- */
/* <<Chunk:NodeGraphContext:GGXTransmission>>--(
<?xml version="1.0" encoding="utf-8"?>
<NodeGraphContext xmlns:i="http://www.w3.org/2001/XMLSchema-instance" xmlns="http://schemas.datacontract.org/2004/07/ShaderPatcherLayer">
	<HasTechniqueConfig>false</HasTechniqueConfig>
	<ShaderParameters xmlns:d2p1="http://schemas.microsoft.com/2003/10/Serialization/Arrays" />
	<Variables xmlns:d2p1="http://schemas.microsoft.com/2003/10/Serialization/Arrays">
		<d2p1:KeyValueOfstringstring>
			<d2p1:Key>iorIncident</d2p1:Key>
			<d2p1:Value>1.f</d2p1:Value>
		</d2p1:KeyValueOfstringstring>
		<d2p1:KeyValueOfstringstring>
			<d2p1:Key>iorOutgoing</d2p1:Key>
			<d2p1:Value>1.05f</d2p1:Value>
		</d2p1:KeyValueOfstringstring>
		<d2p1:KeyValueOfstringstring>
			<d2p1:Key>F0</d2p1:Key>
			<d2p1:Value>.2</d2p1:Value>
		</d2p1:KeyValueOfstringstring>
		<d2p1:KeyValueOfstringstring>
			<d2p1:Key>roughness</d2p1:Key>
			<d2p1:Value>.33</d2p1:Value>
		</d2p1:KeyValueOfstringstring>
		<d2p1:KeyValueOfstringstring>
			<d2p1:Key>n</d2p1:Key>
			<d2p1:Value>Function:BuildRefractionNormal</d2p1:Value>
		</d2p1:KeyValueOfstringstring>
		<d2p1:KeyValueOfstringstring>
			<d2p1:Key>i</d2p1:Key>
			<d2p1:Value>Function:BuildRefractionIncident</d2p1:Value>
		</d2p1:KeyValueOfstringstring>
		<d2p1:KeyValueOfstringstring>
			<d2p1:Key>o</d2p1:Key>
			<d2p1:Value>Function:BuildRefractionOutgoing</d2p1:Value>
		</d2p1:KeyValueOfstringstring>
	</Variables>
</NodeGraphContext>
)-- */
