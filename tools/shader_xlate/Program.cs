using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Text.Json;
using System.Text.Json.Nodes;
using Ryujinx.Graphics.Shader;
using Ryujinx.Graphics.Shader.Translation;
sealed class Accessor(byte[] code,byte[] constants,AlphaTestOp alpha) : IGpuAccessor {
    int binding=1;
    public ReadOnlySpan<ulong> GetCode(ulong address,int minimumSize) {
        if(address>(ulong)code.Length || minimumSize>code.Length-(long)address) throw new Exception("Code outside captured region");
        return MemoryMarshal.Cast<byte,ulong>(code.AsSpan((int)address));
    }
    public SetBindingPair CreateConstantBufferBinding(int index)=>new(0,binding++);
    public SetBindingPair CreateStorageBufferBinding(int index)=>throw new Exception("Storage-buffer evidence missing");
    public SetBindingPair CreateImageBinding(int count,bool isBuffer)=>throw new Exception("Image evidence missing");
    public SetBindingPair CreateTextureBinding(int count,bool isBuffer) { int b=binding;binding+=count;return new(0,b); }
    public int QuerySamplerArrayLengthFromPool()=>throw new Exception("Sampler-array evidence missing");
    public int QueryTextureArrayLengthFromBuffer(int slot)=>throw new Exception("Texture-array evidence missing");
    public int QueryTextureArrayLengthFromPool()=>throw new Exception("Texture-array evidence missing");
    public uint ConstantBuffer1Read(int offset) {
        if (offset<0 || constants.Length-offset<4) throw new Exception("Constant buffer 1 read outside payload");
        return BitConverter.ToUInt32(constants,offset);
    }
    // Specialize to the host adapter's accepted texture target, never a fallback.
    // Every draw using this profile must reject buffer/array/cube/integer resources.
    public SamplerType QuerySamplerType(int handle,int slot=-1)=>SamplerType.Texture2D;
    public TextureFormat QueryTextureFormat(int handle,int slot=-1)=>throw new Exception("Image-format evidence missing");
    public GpuGraphicsState QueryGraphicsState()=>new(false,InputTopology.Triangles,false,TessPatchType.Triangles,TessSpacing.EqualSpacing,false,false,false,false,false,1f,alpha,0f,default,false,default,false,false,true,false);
}
static class MainClass {
    static int Main(string[] args) {
        try {Translate(args);return 0;}
        catch(Exception error) {Console.Error.WriteLine(error.Message);return 1;}
    }
    static void Translate(string[] args) {
        if(args.Length<4 || args.Length>5) throw new ArgumentException("usage: ShaderXlate CODE CONSTANTS OUTPUT_BASE vertex|fragment [always|greater-zero]");
        string alphaName=args.Length==5 ? args[4] : "always";
        AlphaTestOp alpha=alphaName switch {"always"=>AlphaTestOp.Always,"greater-zero"=>AlphaTestOp.Greater,_=>throw new Exception("Unverified alpha-test profile")};
        if(args[3]!="vertex" && args[3]!="fragment") throw new Exception("Stage requires a separately validated translation path");
        var raw=File.ReadAllBytes(args[0]);
        var constants=File.ReadAllBytes(args[1]);
        var context=Translator.CreateContext(0,new Accessor(raw,constants,alpha),new TranslationOptions(TargetLanguage.Spirv,TargetApi.Vulkan,TranslationFlags.DebugMode));
        var program=context.Translate();
        if ((args[3]=="vertex" && program.Info.Stage!=ShaderStage.Vertex) ||
            (args[3]=="fragment" && program.Info.Stage!=ShaderStage.Fragment)) throw new Exception("SPH and manifest stages disagree");
        File.WriteAllBytes(args[2]+".spv",program.BinaryCode);
        var metadata=JsonSerializer.SerializeToNode(program.Info,new JsonSerializerOptions {IncludeFields=true}).AsObject();
        metadata["Profile"]="float-2d-upper-left-zero-one-v1";
        metadata["AlphaTestFunction"]=alpha==AlphaTestOp.Always || program.Info.Stage!=ShaderStage.Fragment ? 8 : 5;
        metadata["AlphaTestReference"]=0f;
        metadata["CodeSha256"]=Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(raw)).ToLowerInvariant();
        metadata["ConstantsSha256"]=Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(constants)).ToLowerInvariant();
        File.WriteAllText(args[2]+".json",metadata.ToJsonString(new JsonSerializerOptions {WriteIndented=true}));
        Console.WriteLine("Translated captured shader; graphics state assumptions still require draw validation.");
    }
}
