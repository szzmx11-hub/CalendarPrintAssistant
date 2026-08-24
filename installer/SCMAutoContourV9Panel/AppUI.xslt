<?xml version="1.0" encoding="UTF-8"?>
<xsl:stylesheet version="1.0"
  xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
  xmlns:frmwrk="Corel Framework Data">
  <xsl:output method="xml" encoding="UTF-8" indent="yes"/>
  <frmwrk:uiconfig>
    <frmwrk:applicationInfo userConfiguration="true"/>
  </frmwrk:uiconfig>
  <xsl:template match="node()|@*">
    <xsl:copy><xsl:apply-templates select="node()|@*"/></xsl:copy>
  </xsl:template>
  <xsl:template match="uiConfig/items">
    <xsl:copy>
      <xsl:apply-templates select="node()|@*"/>
      <itemData
        guid="d5b54e30-e5cd-4b93-9fb5-681746c36c51"
        noBmpOnMenu="true"
        type="checkButton"
        check="*Docker('2e4a3184-6f64-4449-963b-f87bec730dc1')"
        dynamicCategory="2cc24a3e-fe24-4708-9a74-9c75406eebcd"
        userCaption="SCM V9 自动寻边工具"
        enable="true"/>
      <itemData
        guid="edb9a702-770c-43d6-b97a-8026db7e407d"
        type="browser"
        href="[VGAppAddonsDir]/SCMAutoContourV9Panel/panel.html"
        enable="true"/>
    </xsl:copy>
  </xsl:template>
  <xsl:template match="uiConfig/dockers">
    <xsl:copy>
      <xsl:apply-templates select="node()|@*"/>
      <dockerData
        guid="2e4a3184-6f64-4449-963b-f87bec730dc1"
        userCaption="SCM V9 自动寻边工具"
        noPadding="true"
        wantReturn="true"
        focusStyle="noThrow">
        <container>
          <item dock="fill" margin="0,0,0,0" guidRef="edb9a702-770c-43d6-b97a-8026db7e407d"/>
        </container>
      </dockerData>
    </xsl:copy>
  </xsl:template>
</xsl:stylesheet>
