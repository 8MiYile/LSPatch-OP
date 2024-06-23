package com.wind.meditor.visitor;

import com.wind.meditor.property.AttributeItem;
import com.wind.meditor.property.ModificationProperty;
import com.wind.meditor.utils.NodeValue;

import java.util.List;
import pxb.android.axml.NodeVisitor;

/**
 * @author Windysha
 */
public class ApplicationTagVisitor extends ModifyAttributeVisitor {

    private List<ModificationProperty.MetaData> metaDataList;
    private List<ModificationProperty.MetaData> deleteMetaDataList;
    private ModificationProperty.MetaData curMetaData;
    private List<ModificationProperty.Provider> providerList;

    private static final String META_DATA_FLAG = "meta_data_flag";

    ApplicationTagVisitor(NodeVisitor nv, List<AttributeItem> modifyAttributeList,
                          List<ModificationProperty.MetaData> metaDataList,
                          List<ModificationProperty.MetaData> deleteMetaDataList,
                          List<ModificationProperty.Provider> providerList) {
        super(nv, modifyAttributeList);
        this.metaDataList = metaDataList;
        this.deleteMetaDataList = deleteMetaDataList;
        this.providerList = providerList;
    }

    @Override
    public NodeVisitor child(String ns, String name) {
        System.out.println(" ManifestTagVisitor child  --> ns = " + ns + " name = " + name);
        if (META_DATA_FLAG.equals(ns)) {
            NodeVisitor nv = super.child(null, name);
            if (curMetaData != null) {
                return new MetaDataVisitor(nv, new ModificationProperty.MetaData(
                        curMetaData.getName(), curMetaData.getValue()));
            }
        } else if (NodeValue.MetaData.TAG_NAME.equals(name)
                && deleteMetaDataList != null && !deleteMetaDataList.isEmpty()) {
            NodeVisitor nv = super.child(ns, name);
            return new DeleteMetaDataVisitor(nv, deleteMetaDataList);
        }
        return super.child(ns, name);
    }

    private void addChild(ModificationProperty.MetaData data) {
        curMetaData = data;
        child(META_DATA_FLAG, NodeValue.MetaData.TAG_NAME);
        curMetaData = null;
    }

    public static String getStackTrace(Throwable t){
        StringBuilder sb = new StringBuilder();
        for(StackTraceElement element : t.getStackTrace()){
            sb.append(element.toString());
            sb.append("\n");
        }
        return sb.toString();
    }

    @Override
    public void end() {
        if (metaDataList != null) {
            for (ModificationProperty.MetaData data : metaDataList) {
                addChild(data);
            }
        }
        if (providerList != null) {
            for (ModificationProperty.Provider provider : providerList) {
                NodeVisitor nv = super.child(null, "provider");
                new ProviderVisitor(nv, provider);
            }
        }
        super.end();
    }
}
