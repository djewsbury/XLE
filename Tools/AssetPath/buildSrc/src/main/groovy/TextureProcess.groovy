package xle

import org.gradle.api.DefaultTask
import org.gradle.api.tasks.incremental.IncrementalTaskInputs
import org.gradle.api.tasks.*

///////////////////////////////////////////////////////////////////////////////////////////////////

@groovy.transform.InheritConstructors
class TextureProcessError extends java.lang.Exception
{
}

///////////////////////////////////////////////////////////////////////////////////////////////////

class ProcessStep
{
	Iterable<?> getCommandLine(File input, File output)	{ return null; }
	boolean doCheckErrorStream()					{ return true; }

	boolean isIntermediate = false;
	String outputNamePostfix = "";

	static Closure makeIntermediateName = null;
    String asDestinationFileName(File i, String newExt, int intermediateIndex)
    {
        Object intName;
        if (makeIntermediateName != null)	intName = makeIntermediateName(i);
        else								intName = i.getPath();

        return intName.substring(0, intName.lastIndexOf('.')) + outputNamePostfix + (isIntermediate?"_${intermediateIndex}":"") + newExt;
    }
}

class TextureTask extends DefaultTask
{
    File input = null
	File output = null

	List<ProcessStep> steps = new ArrayList<ProcessStep>();
	Map<Integer, Integer> stepInputs = new HashMap<Integer, Integer>();

	@InputFiles
	List<File> getInputFiles()
	{
		def result = new ArrayList<File>();
		if (input) result.add(input);
		return result;
	}

    @OutputFiles
    List<File> getOutputFiles()
	{
		def result = new ArrayList<File>();
		for (int c=0; c<steps.size(); ++c)
			result.add(getOutputFile(c));
		return result;
	}

	File getOutputFile(int index)
	{
		if (index == steps.size()-1 && output) return output;
		return new File(steps[index].asDestinationFileName(input, ".dds", index));
	}

	static void checkProcessErrorStream(Process process, String operation)
	{
		if (process.getErrorStream().available() > 0)
        {
            def reader = new BufferedReader(new InputStreamReader(process.getErrorStream()));
            def builder = new StringBuilder();
            String line = null;
            while ((line = reader.readLine()) != null) {
                builder.append(line);
                builder.append(System.getProperty("line.separator"));
            }

                // Note that using TextureProcessError here causes a JVM exception in my
                // version of the JDK. It's fixed in a newer version; but let's just use
                // a basic exception, anyway
            throw new java.lang.Exception("${builder.toString()} << ${operation} >>")
        }
	}

	@TaskAction
    void run()
	{
			// Run each step one by one... Sometimes we create intermediate steps, which
			// are stored in temporary files

		for (int c=0; c<steps.size(); ++c) {
			def stepInput = input;
			def t = stepInputs[c];
			if (t != null) { stepInput = getOutputFile(t) }
			else if (c!=0) { stepInput = getOutputFile(c-1) }

			def stepOutput = getOutputFile(c)
			def exe = steps[c].getCommandLine(stepInput, stepOutput)

			// def process = cmdLine.execute()
			// process.waitFor()
			// if (steps[c].doCheckErrorStream())
			// 	checkProcessErrorStream(process, "while processing texture from ${stepInput ? stepInput.getAbsolutePath() : "<no-input>"} to output file ${stepOutput.getAbsolutePath()}");

			project.exec( { commandLine = exe });
		}
	}

	/*
	@TaskAction
    void execute(IncrementalTaskInputs inputs)
    {
        println inputs.incremental ? "CHANGED inputs considered out of date"
                                   : "ALL inputs considered out of date"

        inputs.outOfDate { change ->
            def output = asDestinationFileName(change.file)
            def input = change.file.getAbsolutePath()
            def exe = getCommandLine(change.file, output);

            // println "out of date: ${input}"
            // println "writing to ${output}"
            // println exe

            // def process = exe.execute()
            // process.waitFor()
			// checkProcessErrorStream(process, "while processing texture from ${input} to output file ${output}");

			project.exec( { commandLine = exe });
        }

        inputs.removed { change ->
            println "removed: ${change.file.name}"
            new File(asDestinationFileName(change.file)).delete()
        }
    }
	*/

}

///////////////////////////////////////////////////////////////////////////////////////////////////

class TextureCompress extends ProcessStep
{
    @Input
    String compressionMode = "bc1";

    @Input
    String mipFilter = "kaiser"

    @Input
    String wrapping = "repeat"

    @Input
    boolean mips = true

	@Input
	boolean isNormalMap = false

	Iterable<?> getCommandLine(File input, File output)
	{
		def res =
			["nvcompress", "-nocuda", "-silent", "-$compressionMode", "-$wrapping",
			mips?"-mipfilter":"-nomips", mips?"$mipFilter":"",
			isNormalMap?"-normal":"",
			input.getAbsolutePath(), output.getAbsolutePath()];
		return res;
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////

class TextureTransformStep extends ProcessStep
{
	Iterable<?> makeCommandLine(File output, String shader, String parameters)
	{
		def res = ["TextureTransform", "o=${output.getAbsolutePath()}; s=${shader}; ~p; ${parameters}"];
		return res;
	}
}

class EquirectToCube extends TextureTransformStep
{
    @Input
    int faceSize = 1024

    @Input
    String format = "R16G16B16A16_FLOAT"

    @Input
    String shader = "main"

    Iterable<?> getCommandLine(File input, File output)
	{
		return makeCommandLine(
			output,
			"ToolsHelper/EquirectangularToCube.hlsl:${shader}",
			"Input=${input.getAbsolutePath()}; Dims={${faceSize*3}, ${faceSize*4}}; Format=${format}");
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////

class GenericTextureGen extends TextureTransformStep
{
    @Input
    String format = "R16G16B16A16_FLOAT"

    @Input
    String shader;

    @Input
    int width = 256;
    @Input
	int height = 256;
	@Input
	int arrayCount = 1;

	Iterable<?> getCommandLine(File input, File output)
	{
		return makeCommandLine(
			output, shader,
			"Dims={${width}, ${height}}; ArrayCount=${arrayCount}; Format=${format}");
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////

class CompressWithTextureTransform extends TextureTransformStep
{
    @Input
    String format = "BC6H_UF16"

	Iterable<?> getCommandLine(File input, File output)
	{
		return makeCommandLine(output, "Compress", "Format=${format}; Input=${input.getAbsolutePath()}");
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////

class CubeMapGen extends ProcessStep
{
	Iterable<?> getCommandLine(File input, File output)
	{
		def res = ["CubeMapGen",
			input.getAbsolutePath(), "-exit", "-exportCubeDDS",
			"-exportMipChain", "-edgeFixupWidth:0", "-exportPixelFormat:A16B16G16R16F",
			"-exportFilename:${output.getAbsolutePath()}"];
		return res;
	}

	boolean doCheckErrorStream() { return false; }
}

class DiffuseCubeMapGen extends ProcessStep
{
	Iterable<?> getCommandLine(File input, File output)
	{
		def res = ["ModifiedCubeMapGen",
			input.getAbsolutePath(), "-exit", "-IrradianceCubemap:180",
			"-exportCubeDDS", "-exportMipChain", "-edgeFixupWidth:0",
			"-exportPixelFormat:A16B16G16R16F", "-exportSize:32",
			"-exportFilename:${output.getAbsolutePath()}"];
		return res;
	}

	boolean doCheckErrorStream() { return false; }
}

///////////////////////////////////////////////////////////////////////////////////////////////////

class SpecularIBLFilter extends TextureTransformStep
{
    @Input
    String format = "R32G32B32A32_FLOAT"

	@Input
    int faceSize = 512

	Iterable<?> getCommandLine(File input, File output)
	{
		return makeCommandLine(output,
			"ToolsHelper/SplitSum.sh:EquiRectFilterGlossySpecular",
			"MipCount=${(int)(1+Math.log(faceSize)/Math.log(2.0f))}; ArrayCount=6; PassCount=128; Input=${input}; Dims={${faceSize}, ${faceSize}}; Format=${format}");
	}
}

class SpecularTransIBLFilter extends TextureTransformStep
{
    @Input
    String format = "R32G32B32A32_FLOAT"

	@Input
    int faceSize = 512

	Iterable<?> getCommandLine(File input, File output)
	{
		return makeCommandLine(output,
			"ToolsHelper/SplitSum.sh:EquiRectFilterGlossySpecularTrans",
			"MipCount=${(int)(1+Math.log(faceSize)/Math.log(2.0f))}; ArrayCount=6; PassCount=128; Input=${input}; Dims={${faceSize}, ${faceSize}}; Format=${format}");
	}
}


///////////////////////////////////////////////////////////////////////////////////////////////////

// 4bpp RGB default compression (with optional 1 bit alpha)
class BC1 extends TextureTask		{ BC1() { steps.add(new TextureCompress(compressionMode:"bc1")) } }
class BC1a extends TextureTask		{ BC1a() { steps.add(new TextureCompress(compressionMode:"bc1a")) } }

// 8bpp RGBA
// BC2: 4 bits/pixel quantized alpha
// BC3: 4 bits/pixel interpolated alpha
class BC2 extends TextureTask		{ BC2() { steps.add(new TextureCompress(compressionMode:"bc2")) } }
class BC3 extends TextureTask		{ BC3() { steps.add(new TextureCompress(compressionMode:"bc3")) } }

class NormalMap extends TextureTask { NormalMap() { steps.add(new TextureCompress(compressionMode:"bc5", mipFilter:"box", isNormalMap:true)) } }

class BC6 extends TextureTask		{ BC6() { steps.add(new TextureCompress(compressionMode:"bc6")) } }

// 8bpp RGB or RGBA
// high precision new format
class BC7 extends TextureTask		{ BC7() { steps.add(new TextureCompress(compressionMode:"bc7")) } }

class Lumi extends TextureTask		{ Lumi() { steps.add(new TextureCompress(compressionMode:"lumi")) } }

class RGB extends TextureTask		{ RGB() { steps.add(new TextureCompress(compressionMode:"rgb")) } }

///////////////////////////////////////////////////////////////////////////////////////////////////

class EquiRectEnv extends TextureTask
{
	EquiRectEnv()
	{
			// First, convert the input texture to a dds format
			// (even though we're using nvcompress, the result will be in an uncompressed format)
		steps.add(new TextureCompress(compressionMode:"rgb", mips:false, isIntermediate:true));

			// Build the basic cubemap background
			// Use CubeMapGen to create mipmaps for the cubemap that are weighted for solid angle
			// Then compress the result into BC6H_UF16 format
		steps.add(new EquirectToCube(isIntermediate:true));
		steps.add(new CubeMapGen(isIntermediate:true));
		steps.add(new CompressWithTextureTransform(format: "BC6H_UF16"));

			// Use ModifiedCubeMapGen to create the diffuse IBL reflections
			// We will compress the result into BC6H_UF16 format
		steps.add(new DiffuseCubeMapGen(isIntermediate:true));
		stepInputs[4] = 1;
		steps.add(new CompressWithTextureTransform(format: "BC6H_UF16", outputNamePostfix:"_diffuse"));

			// Use TextureTransform to create the specular IBL reflections
			// Then compress the result into BC6H_UF16 format
		steps.add(new SpecularIBLFilter(isIntermediate:true));
		stepInputs[6] = 0;
		steps.add(new CompressWithTextureTransform(format: "BC6H_UF16", outputNamePostfix:"_specular"));

			// Use TextureTransform to create the specular IBL transmissions
			// Then compress the result into BC6H_UF16 format
		steps.add(new SpecularTransIBLFilter(isIntermediate:true));
		stepInputs[8] = 0;
		steps.add(new CompressWithTextureTransform(format: "BC6H_UF16", outputNamePostfix:"_speculartrans"));
	}
}

class HemiEquiRectEnv extends TextureTask
{
	HemiEquiRectEnv()
	{
		steps.add(new TextureCompress(compressionMode:"rgb", mips:false, isIntermediate:true));
		steps.add(new EquirectToCube(isIntermediate:true, shader:"hemi"));
		steps.add(new CubeMapGen());
		steps.add(new DiffuseCubeMapGen(outputNamePostfix:"_diffuse"));
		stepInputs[3] = 1;
	}
}
