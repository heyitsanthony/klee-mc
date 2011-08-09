<html
 xsl:version="1.0"
 xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
 xmlns="http://www.w3.org/1999/xhtml">
<head>
<title>KLEE-MC /bin/ary tests</title>
</head>
<body style="font-family:Arial; font-size:12pt; background-color:#EEEEEE">
KLEE-MC Command Tests
<hr/>
<table>
<tr>
	<td>Command</td>
	<td alt="Done">D</td>
	<td alt="Errors">E</td>
	<td alt="Timeout">T</td>
	<td alt="Syscalls">Sc</td>
	<td alt="Aborted">A</td>
	<td alt="Solver Died">S</td>
	<td>Time</td>
	<td>MB</td>
	<td>DynInst</td>
	<td>DBTInst</td>
</tr>
<xsl:for-each select="testruns/testrun">
<xsl:sort select="command" />
<tr>
	<td style="background-color:teal;color:white;padding:4px; width: 32em;">
		<span style="font-weight:bold">
		<xsl:value-of select="command"/>
		</span>
	</td>

	<xsl:choose>
	<xsl:when test="done"><td style="background-color:green;">O</td></xsl:when>
	<xsl:otherwise><td></td></xsl:otherwise>
	</xsl:choose>

	<xsl:choose>
	<xsl:when test="error"><td style="background-color:yellow;">!</td></xsl:when>
	<xsl:otherwise><td></td></xsl:otherwise>
	</xsl:choose>

	<xsl:choose>
	<xsl:when test="timeout"><td style="background-color:blue;">...</td></xsl:when>
	<xsl:otherwise><td></td></xsl:otherwise>
	</xsl:choose>

	<xsl:choose>
	<xsl:when test="newsyscall"><td style="background-color:orange;">?</td></xsl:when>
	<xsl:otherwise><td></td></xsl:otherwise>
	</xsl:choose>

	<xsl:choose>
	<xsl:when test="abort"><td style="background-color:red;">X</td></xsl:when>
	<xsl:otherwise><td></td></xsl:otherwise>
	</xsl:choose>

	<xsl:choose>
	<xsl:when test="badsolve"><td style="background-color:red;">X</td></xsl:when>
	<xsl:otherwise><td></td></xsl:otherwise>
	</xsl:choose>

	<td style="margin-left:20px;margin-bottom:1em;font-size:10pt; text-align: right; font-family: monospace">
		<xsl:value-of select="round(kstats/WallTime)"/>
	</td>

	<td style="margin-left:20px;margin-bottom:1em;font-size:10pt; text-align: right; font-family: monospace">
		<xsl:value-of select="round(kstats/MemUsedKB div 1024)" />
	</td>


	<td style="margin-left:20px;margin-bottom:1em;font-size:10pt; text-align: right; font-family: monospace">
		<xsl:value-of select="kstats/Instructions"/>
	</td>

	<td style="margin-left:20px;margin-bottom:1em;font-size:10pt; text-align: right; font-family: monospace">
		<xsl:value-of select="kstats/CoveredInstructions + kstats/UncoveredInstructions"/>
	</td>

</tr>
</xsl:for-each>
</table>
<ul>
<li>Total Runs: <xsl:value-of select="count(//testrun)"/></li>
<li>Total Timeouts: <xsl:value-of select="count(//timeout)"/></li>
<li>Total Solver Explosions: <xsl:value-of select="count(//badsolve)"/></li>
<li>Total Unimplemented Syscall: <xsl:value-of select="count(//newsyscall)"/></li>
<li>Total LLVM Instructions from VEXLLVM: <xsl:value-of select="sum(//kstats/UncoveredInstructions) + sum(//kstats/CoveredInstructions)"/></li>
<li>Total LLVM Instructions dispatched: <xsl:value-of select="sum(//kstats/Instructions)"/></li>
</ul>

</body>
</html>