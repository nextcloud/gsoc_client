<xsl:stylesheet version="1.0" 
xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
xmlns:wi="http://schemas.microsoft.com/wix/2006/wi">
  <xsl:key name="vIdToReplace" match="wi:ComponentGroup[@Id='Files']/wi:Component[wi:File[contains(@Source,'$MainExecutable')]]" use="@Id"/>

  <xsl:template match="@*|node()">
    <xsl:copy>
      <xsl:apply-templates select="@*|node()" />
    </xsl:copy>
  </xsl:template>

  <xsl:template match="node()[key('vIdToReplace', @Id)]">
    <xsl:copy>
      <xsl:attribute name="Id">Component_Main_Executable</xsl:attribute>
      <xsl:copy-of select="@*[name()!='Id']"/>
      <xsl:apply-templates />
    </xsl:copy>
  </xsl:template>

  <xsl:template match="wi:ComponentGroup[@Id='Files']/wi:Component/wi:File[contains(@Source,'$MainExecutable')]">
     <xsl:copy>
        <xsl:attribute name="Id">Main_Executable</xsl:attribute>
        <xsl:copy-of select="@*[name()!='Id']"/>
        <xsl:apply-templates />
     </xsl:copy>
  </xsl:template>
</xsl:stylesheet>
