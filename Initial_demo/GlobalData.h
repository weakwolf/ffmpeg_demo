#pragma once

/**
 * ȫ�����ݲ㣬�Ե���ģʽʵ��
 */

class CGlobalData
{
public:
	CGlobalData& GetInstance() { return instance; }

public:
	void	SetFilePath(const CString& szFilePath) { m_szFilePath = szFilePath; }
	CString	GetFilePath() const { return m_szFilePath; }

private:
	CGlobalData();
	CGlobalData(const CGlobalData& other) = delete;
	CGlobalData& operator=(const CGlobalData& other) = delete;
	~CGlobalData();

private:
	static CGlobalData instance;

private:
	CString m_szFilePath;	// �ļ�·��
};

