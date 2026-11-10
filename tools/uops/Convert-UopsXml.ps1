<#
.SYNOPSIS
  Converts the uops.info instruction database (instructions.xml, ~140 MB) into a flat TSV table:
  one line per (instruction variant, microarchitecture), so it can be searched with rg / Select-String
  in milliseconds instead of loading the XML.

.DESCRIPTION
  Download instructions.xml from https://uops.info/xml.html into tools/uops/data/ first.
  Output columns (tab separated):
    instr     e.g. "VPSRLVQ (YMM, YMM, YMM)"
    arch      e.g. ICL, ADL-P, ZEN4 (uops.info abbreviations)
    uops      number of fused-domain uops
    ports     port usage, e.g. "1*p01"
    tp        reciprocal throughput in cycles (measured, loop variant; unrolled if loop is missing)
    lat       latency in cycles, "min" or "min..max" over all operand pairs ("" if not measured)
    lat_detail  per operand pair: "src->dst:cycles", ';' separated (bounds like "<=4" kept as given)
    ext       ISA extension, e.g. AVX2
    iform     XED iform, e.g. VPSRLVQ_YMMqq_YMMqq_YMMqq
    summary   short description
  Only measured data is exported (IACA predictions are skipped).

.EXAMPLE
  ./tools/uops/Convert-UopsXml.ps1
#>
param(
	[string]$Xml = "$PSScriptRoot/data/instructions.xml",
	[string]$Out = "$PSScriptRoot/data/uops.tsv"
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path $Xml)) { throw "Not found: $Xml. Download instructions.xml from https://uops.info/xml.html" }

Add-Type -Language CSharp -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using System.Xml;

public static class UopsXmlConverter
{
	static string Clean(string s) { return s == null ? "" : s.Replace('\t', ' ').Replace('\n', ' ').Replace('\r', ' '); }

	public static int Convert(string xmlPath, string outPath)
	{
		var settings = new XmlReaderSettings { IgnoreWhitespace = true, IgnoreComments = true, DtdProcessing = DtdProcessing.Ignore };
		int rows = 0;
		using (var reader = XmlReader.Create(xmlPath, settings))
		using (var w = new StreamWriter(outPath, false, new UTF8Encoding(false)))
		{
			w.Write("instr\tarch\tuops\tports\ttp\tlat\tlat_detail\text\tiform\tsummary\n");
			string instr = "", ext = "", iform = "", summary = "", arch = "";
			bool inMeasurement = false;
			string uops = "", ports = "", tp = "";
			var lats = new List<string>();
			int latMin = int.MaxValue, latMax = int.MinValue;

			while (reader.Read())
			{
				if (reader.NodeType == XmlNodeType.Element)
				{
					switch (reader.Name)
					{
					case "instruction":
						instr = Clean(reader.GetAttribute("string"));
						ext = Clean(reader.GetAttribute("extension"));
						iform = Clean(reader.GetAttribute("iform"));
						summary = Clean(reader.GetAttribute("summary"));
						break;
					case "architecture":
						arch = reader.GetAttribute("name") ?? "";
						break;
					case "measurement":
						inMeasurement = !reader.IsEmptyElement;
						uops = reader.GetAttribute("uops") ?? "";
						ports = reader.GetAttribute("ports") ?? "";
						tp = reader.GetAttribute("TP_loop") ?? reader.GetAttribute("TP_unrolled") ?? reader.GetAttribute("TP") ?? "";
						lats.Clear();
						latMin = int.MaxValue; latMax = int.MinValue;
						if (!inMeasurement) { WriteRow(w, instr, arch, uops, ports, tp, "", "", ext, iform, summary); rows++; }
						break;
					case "latency":
						if (inMeasurement)
						{
							string cyc = reader.GetAttribute("cycles")
								?? reader.GetAttribute("cycles_mem")
								?? reader.GetAttribute("cycles_addr")
								?? reader.GetAttribute("max_cycles")
								?? reader.GetAttribute("max_cycles_mem")
								?? reader.GetAttribute("max_cycles_addr");
							string bound = reader.GetAttribute("cycles") == null && cyc != null ? "<=" : "";
							int c;
							if (cyc != null && int.TryParse(cyc, out c)) { latMin = Math.Min(latMin, c); latMax = Math.Max(latMax, c); }
							lats.Add(reader.GetAttribute("start_op") + "->" + reader.GetAttribute("target_op") + ":" + bound + cyc);
						}
						break;
					}
				}
				else if (reader.NodeType == XmlNodeType.EndElement && reader.Name == "measurement" && inMeasurement)
				{
					string lat = latMin == int.MaxValue ? "" : (latMin == latMax ? latMin.ToString() : latMin + ".." + latMax);
					WriteRow(w, instr, arch, uops, ports, tp, lat, string.Join(";", lats), ext, iform, summary);
					rows++;
					inMeasurement = false;
				}
			}
		}
		return rows;
	}

	static void WriteRow(StreamWriter w, params string[] cols)
	{
		w.Write(string.Join("\t", cols));
		w.Write('\n');
	}
}
'@

$sw = [Diagnostics.Stopwatch]::StartNew()
$rows = [UopsXmlConverter]::Convert((Resolve-Path $Xml).Path, [IO.Path]::GetFullPath($Out))
"Wrote $rows rows to $Out in $([int]$sw.Elapsed.TotalSeconds) s"
